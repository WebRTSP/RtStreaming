#include "GstStreamingSource.h"

#include <cassert>

#include <netdb.h>
#include <arpa/inet.h>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>

#include <CxxPtr/GlibPtr.h>

#include "Helpers.h"
#include "GstWebRTCPeer2.h"


GstStreamingSource::GstStreamingSource()
{
}

GstStreamingSource::~GstStreamingSource()
{
    assert(_peers.empty());
}

gboolean GstStreamingSource::onBusMessage(GstMessage* message)
{
    switch(GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_EOS:
            onEos(false);
            break;
        case GST_MESSAGE_ERROR: {
            gchar* debug;
            GError* error;

            gst_message_parse_error(message, &error, &debug);

            g_free(debug);
            g_error_free(error);

            onEos(true);
            break;
        }
        case GST_MESSAGE_APPLICATION: {
            const GstStructure* structure =
                gst_message_get_structure(message);
            assert(structure);
            if(!structure)
                break;

            MessageProxy* target = nullptr;
            if(gst_structure_get(structure, "target", MESSAGE_PROXY_TYPE, &target, nullptr)) {
                g_signal_emit_by_name(target, "message", message);
                g_object_unref(target);

                break;
            }

            if(gst_message_has_name(message, "tee-pad-removed")) {
                onTeePadRemoved();
            } else if(gst_message_has_name(message, "eos")) {
                gboolean error = FALSE;
                gst_structure_get_boolean(structure, "error", &error);
                onEos(error != FALSE);
            }

            break;
        }
        default:
            break;
    }

    return TRUE;
}

void GstStreamingSource::onEos(bool error)
{
    for(MessageProxy* target: _peers) {
        g_signal_emit_by_name(target, "eos", error);
    }
}

// will be called from streaming thread
void GstStreamingSource::postEos(
    GstElement* rtcbin,
    gboolean error)
{
    GstStructure* structure =
        gst_structure_new(
            "eos",
            "error", G_TYPE_BOOLEAN, error,
            nullptr);

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(rtcbin), structure);

    GstBusPtr busPtr(gst_element_get_bus(rtcbin));
    gst_bus_post(busPtr.get(), message);
}

void GstStreamingSource::setPipeline(GstElementPtr&& pipelinePtr) noexcept
{
    assert(pipelinePtr);
    assert(!_pipelinePtr);

    if(!pipelinePtr || _pipelinePtr)
        return;

    _pipelinePtr = std::move(pipelinePtr);
    GstElement* pipeline = this->pipeline();

    auto onBusMessageCallback =
        (gboolean (*) (GstBus*, GstMessage*, gpointer))
        [] (GstBus* bus, GstMessage* message, gpointer userData) -> gboolean {
            GstStreamingSource* self = static_cast<GstStreamingSource*>(userData);
            return self->onBusMessage(message);
        };
    GstBusPtr busPtr(gst_pipeline_get_bus(GST_PIPELINE(pipeline)));
    gst_bus_add_watch(busPtr.get(), onBusMessageCallback, this);
}

GstElement* GstStreamingSource::pipeline() const noexcept
{
    return _pipelinePtr.get();
}

void GstStreamingSource::onTeePadRemoved()
{
    gint teeSrcPadsCount = 0;
    g_object_get(G_OBJECT(tee()), "num-src-pads", &teeSrcPadsCount, nullptr);

    if(teeSrcPadsCount == 1) { // only fakesink is linked
        gst_element_set_state(pipeline(), GST_STATE_NULL);
        cleanup();
    }
}

// will be called from streaming thread
void GstStreamingSource::postTeePadRemoved(GstElement* tee)
{
    GstStructure* structure =
        gst_structure_new_empty("tee-pad-removed");

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(tee), structure);

    GstBusPtr busPtr(gst_element_get_bus(tee));
    if(busPtr)
        gst_bus_post(busPtr.get(), message);
}

void GstStreamingSource::setTee(GstElementPtr&& teePtr) noexcept
{
    assert(teePtr);
    assert(!_teePtr);

    if(!teePtr || _teePtr)
        return;

    _teePtr = std::move(teePtr);
    GstElement* tee = _teePtr.get();

    auto onPadRemovedCallback =
        (void (*) (GstElement*, GstPad*, gpointer*))
        [] (GstElement* tee, GstPad* pad, gpointer* userData) {
            postTeePadRemoved(tee);
        };
    g_signal_connect(tee, "pad-removed",
        G_CALLBACK(onPadRemovedCallback), pipeline());

    GstPadPtr fakeSinkTeePadPtr(gst_element_get_request_pad(tee, "src_%u"));
    GstPad* fakeSinkTeePad = fakeSinkTeePadPtr.get();

    _fakeSinkPtr.reset(gst_element_factory_make("fakesink", nullptr));
    GstElement* fakeSink = _fakeSinkPtr.get();
    g_object_set(fakeSink, "sync", TRUE, NULL);
    gst_bin_add(GST_BIN(pipeline()), GST_ELEMENT(gst_object_ref(fakeSink)));
    gst_element_sync_state_with_parent(fakeSink);
    GstPadPtr fakeSinkPadPtr(gst_element_get_static_pad(fakeSink, "sink"));
    GstPad* fakeSinkPad = fakeSinkPadPtr.get();

    if(GST_PAD_LINK_OK != gst_pad_link(fakeSinkTeePad, fakeSinkPad)) {
        g_assert(false);
    }

    for(MessageProxyPtr& proxyPtr: _waitingPeers) {
        g_signal_emit_by_name(proxyPtr.get(), "tee", tee);
    }
    _waitingPeers.clear();
}

GstElement* GstStreamingSource::tee() const noexcept
{
    return _teePtr.get();
}

void GstStreamingSource::cleanup() noexcept
{
    _teePtr.reset();
    _fakeSinkPtr.reset();
    _pipelinePtr.reset();
}

void GstStreamingSource::peerDestroyed(MessageProxy* messageProxy)
{
    _peers.erase(messageProxy);
}

std::unique_ptr<WebRTCPeer> GstStreamingSource::createPeer() noexcept
{
    if(!pipeline())
        prepare();

    if(!pipeline())
        return nullptr;

    MessageProxyPtr messageProxyPtr(message_proxy_new());
    MessageProxy* messageProxy = messageProxyPtr.get();

    g_object_weak_ref(G_OBJECT(messageProxy),
        [] (gpointer data, GObject* object) {
            GstStreamingSource* self = static_cast<GstStreamingSource*>(data);
            self->peerDestroyed(_MESSAGE_PROXY(object));
        }, this);

    _peers.insert(messageProxy);

    std::unique_ptr<GstWebRTCPeer2> peerPtr =
        std::make_unique<GstWebRTCPeer2>(messageProxy, pipeline());
    if(tee()) {
        g_signal_emit_by_name(messageProxy, "tee", tee());
    } else {
        _waitingPeers.emplace_back(std::move(messageProxyPtr));
    }

    return std::move(peerPtr);
}
