#include "GstRecordStreamer.h"

#include <cassert>

#include <chrono>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstPtr.h>
#include <CxxPtr/GstWebRtcPtr.h>

#include "Helpers.h"
#include "GstRecordPeer.h"


GstRecordStreamer::GstRecordStreamer(
    const std::optional<RecordOptions>& recordOptions,
    const RecorderConnectedCallback& recorderConnectedCallback,
    const RecorderConnectedCallback& recorderDisconnectedCallback) :
    _recordOptions(recordOptions),
    _recorderConnectedCallback(recorderConnectedCallback),
    _recorderDisconnectedCallback(recorderDisconnectedCallback)
{
}

GstElement* GstRecordStreamer::webRtcBin() const noexcept
{
    return _rtcbinPtr.get();
}

bool GstRecordStreamer::prepare() noexcept
{
    // only record peer can prepare
    return pipeline() ? true : false;
}

void GstRecordStreamer::recordPrepare() noexcept
{
    assert(!pipeline());
    if(pipeline())
        return;

    assert(!webRtcBin());
    if(webRtcBin())
        return;

    GstElementPtr pipelinePtr(gst_pipeline_new(nullptr));
    GstElement* pipeline = pipelinePtr.get();

    GstElementPtr rtcbinPtr(gst_element_factory_make("webrtcbin", "record rtcbin"));
    GstElement* rtcbin = rtcbinPtr.get();

    gst_bin_add(GST_BIN(pipeline), GST_ELEMENT(gst_object_ref(rtcbin)));

    GstCapsPtr capsPtr(gst_caps_from_string("application/x-rtp, media=video"));
    GstWebRTCRTPTransceiver* recvonlyTransceiver = nullptr;
    g_signal_emit_by_name(
        rtcbin, "add-transceiver",
        GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY, capsPtr.get(),
        &recvonlyTransceiver);
    GstWebRTCRTPTransceiverPtr recvonlyTransceiverPtr(recvonlyTransceiver);

    auto srcPadAddedCallback =
        (void (*)(GstElement*, GstPad*, gpointer))
        [] (GstElement* rtcbin, GstPad* pad, gpointer userData) {
            GstRecordStreamer* self = static_cast<GstRecordStreamer*>(userData);
            self->srcPadAdded(rtcbin, pad);
        };
    _padAddedHandlerId =
        g_signal_connect(rtcbin, "pad-added", G_CALLBACK(srcPadAddedCallback), this);

    auto noMorePadsCallback =
        (void (*)(GstElement*,  gpointer))
        [] (GstElement* rtcbin, gpointer userData) {
            GstRecordStreamer* self = static_cast<GstRecordStreamer*>(userData);
            self->noMorePads(rtcbin);
        };
    _noMorePadsHandlerId =
        g_signal_connect(rtcbin, "no-more-pads", G_CALLBACK(noMorePadsCallback), this);

    setPipeline(std::move(pipelinePtr));
    _rtcbinPtr = std::move(rtcbinPtr);

    play();
}

namespace {

struct FormatLocationData {
    std::filesystem::path recordingsDir;
};

struct DtsPtsFixData {
    GstElementPtr padOwnerPtr;
};

GstPadProbeReturn
DtsPtsFixProbeFunc(
    GstPad* srcPad,
    GstPadProbeInfo* info,
    gpointer userData)
{
    if(!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER))
        return GST_PAD_PROBE_PASS;

    DtsPtsFixData& data = *static_cast<DtsPtsFixData*>(userData);
    if(!data.padOwnerPtr) {
        data.padOwnerPtr.reset(gst_pad_get_parent_element(srcPad));
    }

    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    GST_BUFFER_DTS(buffer) = gst_element_get_current_running_time(data.padOwnerPtr.get());
    GST_BUFFER_PTS(buffer) = GST_BUFFER_DTS(buffer);

    return GST_PAD_PROBE_PASS;
}

}

// will be called from streaming thread
void GstRecordStreamer::srcPadAdded(
    GstElement* /*rtcbin*/,
    GstPad* pad)
{
    GstElement* pipeline = this->pipeline();

    GstPad *sink;

    if(GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
        return;

    bool recordDirAvailable = false;
    if(_recordOptions) {
        std::error_code errorCode;
        std::filesystem::create_directories(_recordOptions->dir, errorCode);
        recordDirAvailable = errorCode == std::error_code();
    }

    GstElement* transformBin;
    if(isRecordToStorageEnabled() && _recordOptions && recordDirAvailable) {
        if(GstRtStreaming::IsTimestamperAvailable()) {
            transformBin =
                gst_parse_bin_from_description(
                    "rtph264depay ! tee name=record-tee "
                    "record-tee. ! queue name=record-queue leaky=upstream ! h264parse ! h264timestamper ! splitmuxsink muxer=\"mp4mux\" name=record-sink "
                    "record-tee. ! h264parse config-interval=-1 ! rtph264pay pt=96 ! capssetter caps=\"application/x-rtp,profile-level-id=(string)42c015\" ",
                    TRUE, NULL);
        } else {
            transformBin =
                gst_parse_bin_from_description(
                    "rtph264depay ! tee name=record-tee "
                    "record-tee. ! queue name=record-queue leaky=upstream ! h264parse name=record-parse ! splitmuxsink muxer=\"mp4mux\" name=record-sink "
                    "record-tee. ! h264parse config-interval=-1 ! rtph264pay pt=96 ! capssetter caps=\"application/x-rtp,profile-level-id=(string)42c015\" ",
                    TRUE, NULL);

            GstElementPtr parsePtr(gst_bin_get_by_name(GST_BIN(transformBin), "record-parse"));
            GstPadPtr parseSrcPadPtr(gst_element_get_static_pad(parsePtr.get(), "src"));
            gst_pad_add_probe(
                parseSrcPadPtr.get(),
                GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
                DtsPtsFixProbeFunc,
                new DtsPtsFixData,
                [] (void* userData) { delete static_cast<DtsPtsFixData*>(userData); });
        }

        GstElementPtr splitmuxsinkPtr(gst_bin_get_by_name(GST_BIN(transformBin), "record-sink"));
        g_object_set(
            G_OBJECT(splitmuxsinkPtr.get()),
            "max-size-bytes",
            std::max<guint64>(_recordOptions->maxFileSize, 1ull << 20), // >= 1Mb
            NULL);

        g_signal_connect_data(
            splitmuxsinkPtr.get(),
            "format-location",
            G_CALLBACK(
                + [] (GstElement* splitmuxsink, guint fragmentId, gpointer userData) -> gchararray {
                    const FormatLocationData& data = *static_cast<FormatLocationData*>(userData);

                    using namespace std::chrono;
                    auto timestamp = duration_cast<milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    const std::string fileName = std::to_string(timestamp) + ".mp4";
                    std::string locationTemplate = (data.recordingsDir / fileName).string();

                    GstPipelineOwner::PostLog(
                        splitmuxsink,
                        spdlog::level::info,
                        fmt::format("Saving to \"{}\"...", fileName));

                    return g_strdup(locationTemplate.c_str());
                }
            ),
            new FormatLocationData { _recordOptions->dir },
            [] (gpointer userData, GClosure*) { delete(static_cast<FormatLocationData*>(userData)); },
            GConnectFlags());
    } else {
        transformBin =
            gst_parse_bin_from_description(
                "rtph264depay ! h264parse config-interval=-1 ! rtph264pay pt=96 ! "
                "capssetter caps=\"application/x-rtp,profile-level-id=(string)42c015\"",
                TRUE, NULL);
    }
    gst_element_set_name(transformBin, "transform-bin");
    gst_bin_add(GST_BIN(pipeline), transformBin);
    gst_element_sync_state_with_parent(transformBin);

    sink = (GstPad*)transformBin->sinkpads->data;

    if(GST_PAD_LINK_OK != gst_pad_link(pad, sink))
        assert(false);

    GstElementPtr teePtr(gst_element_factory_make("tee", nullptr));
    GstElement* tee = teePtr.get();
    gst_bin_add(GST_BIN(pipeline), GST_ELEMENT(gst_object_ref(tee)));
    gst_element_sync_state_with_parent(tee);
    gst_element_link(transformBin, tee);

    setTee(tee);
}

void GstRecordStreamer::noMorePads(GstElement* /*decodebin*/)
{
}

namespace {

struct FinalizeRecordingData
{
    std::atomic_flag guard = ATOMIC_FLAG_INIT;

    GstElementPtr pipelinePtr;
    GstElementPtr queuePtr;
};

GstPadProbeReturn
FinalizeRecording(
    GstPad* teeSrcPad,
    GstPadProbeInfo*,
    gpointer userData)
{
    FinalizeRecordingData* data = static_cast<FinalizeRecordingData*>(userData);

    if(data->guard.test_and_set())
        return GST_PAD_PROBE_OK;

    GstElement* pipeline = data->pipelinePtr.get();
    GstElementPtr teePtr(gst_bin_get_by_name(GST_BIN(pipeline), "record-tee"));
    GstElement* tee = teePtr.get();
    GstElement* queue = data->queuePtr.get();

    GstPadPtr queueSinkPadPtr(gst_element_get_static_pad(queue, "sink"));
    gst_pad_unlink(teeSrcPad, queueSinkPadPtr.get());
    gst_element_release_request_pad(tee, teeSrcPad);

    gst_pad_send_event(queueSinkPadPtr.get(), gst_event_new_eos());

    return GST_PAD_PROBE_REMOVE;
}

gboolean FinalizeRecordingBusMessageCallback(GstBus* bus, GstMessage* message, gpointer userData)
{
    if(GST_MESSAGE_TYPE(message) != GST_MESSAGE_ELEMENT)
        return TRUE;

    const GstStructure* messageStructure = gst_message_get_structure(message);
    if(!messageStructure)
        return TRUE;

    if(!gst_structure_has_name(messageStructure, "GstBinForwarded"))
        return TRUE;

    GstMessagePtr forwardedMessagePtr;
    GstMessage* forwardedMessage = nullptr;
    gst_structure_get(messageStructure, "message", GST_TYPE_MESSAGE, &forwardedMessage, NULL);
    if(!forwardedMessage)
        return TRUE;

    forwardedMessagePtr.reset(forwardedMessage);

    if(0 != g_strcmp0(GST_OBJECT_NAME(GST_MESSAGE_SRC(forwardedMessage)), "record-sink"))
        return TRUE;

    FinalizeRecordingData* data = static_cast<FinalizeRecordingData*>(userData);

    gst_element_set_state(data->pipelinePtr.get(), GST_STATE_NULL);

    return FALSE;
}

}

void GstRecordStreamer::finalizeRecording(GstElement* pipeline)
{
    if(!pipeline) return;

    GstElementPtr pipelinePtr(pipeline);

    GstElementPtr queuePtr(gst_bin_get_by_name(GST_BIN(pipeline), "record-queue"));
    if(!queuePtr) return;

    GstElementPtr transformBinPtr(gst_bin_get_by_name(GST_BIN(pipeline), "transform-bin"));

    GstPadPtr queueSinkPadPtr(gst_element_get_static_pad(queuePtr.get(), "sink"));
    GstPadPtr teeSrcPadPtr(gst_pad_get_peer(queueSinkPadPtr.get()));

    g_object_set(G_OBJECT(transformBinPtr.get()), "message-forward", TRUE, NULL);
    g_object_set(G_OBJECT(pipeline), "message-forward", TRUE, NULL);
    GstBusPtr busPtr(gst_pipeline_get_bus(GST_PIPELINE(pipeline)));

    FinalizeRecordingData* busWatchData = new FinalizeRecordingData {
        .pipelinePtr = GstElementPtr(GST_ELEMENT(gst_object_ref(pipeline))),
        .queuePtr = GstElementPtr(GST_ELEMENT(gst_object_ref(queuePtr.get())))
    };
    gst_bus_add_watch_full(
        busPtr.get(),
        G_PRIORITY_DEFAULT,
        FinalizeRecordingBusMessageCallback,
        busWatchData,
        [] (void* userData) { delete static_cast<FinalizeRecordingData*>(userData); });

    FinalizeRecordingData* probeData = new FinalizeRecordingData {
        .pipelinePtr = GstElementPtr(GST_ELEMENT(gst_object_ref(pipeline))),
        .queuePtr = GstElementPtr(GST_ELEMENT(gst_object_ref(queuePtr.get())))
    };
    gst_pad_add_probe(
        teeSrcPadPtr.get(),
        GST_PAD_PROBE_TYPE_IDLE,
        FinalizeRecording,
        probeData,
        [] (void* userData) { delete static_cast<FinalizeRecordingData*>(userData); });
}

void GstRecordStreamer::cleanup() noexcept
{
    GstElement* rtcbin = _rtcbinPtr.get();
    if(!rtcbin) return;

    if(_padAddedHandlerId) {
        g_signal_handler_disconnect(rtcbin, _padAddedHandlerId);
        _padAddedHandlerId = 0;
    }
    if(_noMorePadsHandlerId) {
        g_signal_handler_disconnect(rtcbin, _noMorePadsHandlerId);
        _noMorePadsHandlerId = 0;
    }

    _rtcbinPtr.reset();

    assert(_recordPeerProxy == nullptr);

    if(isRecordToStorageEnabled())
        finalizeRecording(releasePipeline());
    else
        GstStreamingSource::cleanup();
}

void GstRecordStreamer::onRecordPeerDestroyed(MessageProxy* messageProxy)
{
    _recordPeerProxy = nullptr;

    if(hasPeers())
        destroyPeers();
    else
        cleanup();

    if(_recorderDisconnectedCallback)
        _recorderDisconnectedCallback();
}

std::unique_ptr<WebRTCPeer> GstRecordStreamer::createRecordPeer() noexcept
{
    if(!pipeline())
        recordPrepare();

    if(!pipeline())
        return nullptr;

    if(!webRtcBin())
        return nullptr;

    if(_recordPeerProxy)
        return nullptr; // recording peer already exists

    MessageProxyPtr messageProxyPtr(message_proxy_new());
    _recordPeerProxy = messageProxyPtr.get();

    g_object_weak_ref(G_OBJECT(_recordPeerProxy),
        [] (gpointer data, GObject* object) {
            GstRecordStreamer* self = static_cast<GstRecordStreamer*>(data);
            self->onRecordPeerDestroyed(_MESSAGE_PROXY(object));
        }, this);

    std::unique_ptr<GstRecordPeer> recordPeerPtr =
        std::make_unique<GstRecordPeer>(_recordPeerProxy, pipeline(), webRtcBin());

    return std::move(recordPeerPtr);
}


void GstRecordStreamer::onPrerolled() noexcept
{
    if(_recorderConnectedCallback)
        _recorderConnectedCallback();
}

void GstRecordStreamer::onPeerAttached() noexcept
{
    // just to ignore implementation from parent
}

void GstRecordStreamer::onLastPeerDetached() noexcept
{
    // pipeline should be active while record peer is active
    if(!_recordPeerProxy)
        cleanup();
}
