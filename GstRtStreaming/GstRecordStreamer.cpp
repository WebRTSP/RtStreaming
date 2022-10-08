#include "GstRecordStreamer.h"

#include <cassert>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstPtr.h>
#include <CxxPtr/GstWebRtcPtr.h>

#include "Helpers.h"
#include "GstRecordPeer.h"


GstRecordStreamer::GstRecordStreamer()
{
}

GstElement* GstRecordStreamer::webRtcBin() const noexcept
{
    return _rtcbinPtr.get();
}

bool GstRecordStreamer::prepare() noexcept
{
    // only record peer can prepare
    return true;
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

    GstElementPtr rtcbinPtr(gst_element_factory_make("webrtcbin", nullptr));
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
    g_signal_connect(rtcbin, "pad-added", G_CALLBACK(srcPadAddedCallback), this);

    auto noMorePadsCallback =
        (void (*)(GstElement*,  gpointer))
        [] (GstElement* rtcbin, gpointer userData) {
            GstRecordStreamer* self = static_cast<GstRecordStreamer*>(userData);
            self->noMorePads(rtcbin);
        };
    g_signal_connect(rtcbin, "no-more-pads", G_CALLBACK(noMorePadsCallback), this);

    setPipeline(std::move(pipelinePtr));
    _rtcbinPtr = std::move(rtcbinPtr);

    play();
}

void GstRecordStreamer::srcPadAdded(
    GstElement* /*rtcbin*/,
    GstPad* pad)
{
    GstElement* pipeline = this->pipeline();

    GstPad *sink;

    if(GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
        return;

    GstElement* transformBin =
        gst_parse_bin_from_description(
            "rtph264depay ! h264parse config-interval=-1 ! rtph264pay pt=96 ! "
            "capssetter caps=\"application/x-rtp,profile-level-id=(string)42c015\"",
            TRUE, NULL);
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

    setTee(std::move(teePtr));
}

void GstRecordStreamer::noMorePads(GstElement* /*decodebin*/)
{
}

void GstRecordStreamer::cleanup() noexcept
{
    _rtcbinPtr.reset();

    assert(_recordPeerProxy == nullptr);

    GstStreamingSource::cleanup();
}

void GstRecordStreamer::recordPeerDestroyed(MessageProxy* messageProxy)
{
    _recordPeerProxy = nullptr;

    if(hasPeers())
        destroyPeers();
    else
        cleanup();
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
            self->recordPeerDestroyed(_MESSAGE_PROXY(object));
        }, this);

    std::unique_ptr<GstRecordPeer> recordPeerPtr =
        std::make_unique<GstRecordPeer>(_recordPeerProxy, pipeline(), webRtcBin());

    return std::move(recordPeerPtr);
}

void GstRecordStreamer::lastPeerDetached() noexcept
{
    // pipeline should be active while record peer is active
    if(!_recordPeerProxy)
        cleanup();
}
