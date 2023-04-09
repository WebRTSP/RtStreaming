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

    cleanup();
}

void GstStreamingSource::setState(GstState state) noexcept
{
    if(!pipeline()) {
        if(state != GST_STATE_NULL)
            ;
        return;
    }

    switch(gst_element_set_state(pipeline(), state)) {
        case GST_STATE_CHANGE_FAILURE:
            break;
        case GST_STATE_CHANGE_SUCCESS:
            break;
        case GST_STATE_CHANGE_ASYNC:
            break;
        case GST_STATE_CHANGE_NO_PREROLL:
            break;
    }
}

void GstStreamingSource::pause() noexcept
{
    setState(GST_STATE_PAUSED);
}

void GstStreamingSource::play() noexcept
{
    setState(GST_STATE_PLAYING);
}

void GstStreamingSource::stop() noexcept
{
    setState(GST_STATE_NULL);
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

            if(gst_structure_has_field(structure, "target")) {
                MessageProxy* target = nullptr;
                if(gst_structure_get(structure, "target", MESSAGE_PROXY_TYPE, &target, nullptr)) {
                    g_signal_emit_by_name(target, "message", message);
                    g_object_unref(target);
                }
                break;
            }

            if(gst_message_has_name(message, "tee")) {
                onTeeAvailable(GST_ELEMENT(GST_MESSAGE_SRC(message)));
            } else if(gst_message_has_name(message, "tee-pad-added"))
                onTeePadAdded();
            else if(gst_message_has_name(message, "tee-pad-removed"))
                onTeePadRemoved();
            else if(gst_message_has_name(message, "eos")) {
                gboolean error = FALSE;
                gst_structure_get_boolean(structure, "error", &error);
                onEos(error != FALSE);
            } else if(gst_message_has_name(message, "log")) {
                gint level = spdlog::level::trace;
                gst_structure_get_int(structure, "level", &level);

                const gchar* message = gst_structure_get_string(structure, "message");
                if(message)
                    log()->log(static_cast<spdlog::level::level_enum>(level), message);
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
    _waitingPeers.clear();

    for(MessageProxy* target: _peers) {
        g_signal_emit_by_name(target, "eos", error);
    }
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

unsigned GstStreamingSource::peerCount() const noexcept
{
    GstElement* tee = this->tee();
    if(!tee)
        return 0;

    gint teeSrcPadsCount = 0;
    g_object_get(G_OBJECT(tee), "num-src-pads", &teeSrcPadsCount, nullptr);

    assert(teeSrcPadsCount > 0); // at least fakesink should be attached
    return teeSrcPadsCount ? teeSrcPadsCount - 1 : 0; // 1 - for linked fakesink
}

bool GstStreamingSource::hasPeers() const noexcept
{
    return peerCount() > 0;
}

void GstStreamingSource::onTeeAvailable(GstElement* tee)
{
    GstElementPtr teePipelinePtr(GST_ELEMENT(gst_object_get_parent(GST_OBJECT(tee))));

    if(teePipelinePtr == _pipelinePtr) { // однако за время пути, собачка могла подрасти...
        _teePtr.reset(GST_ELEMENT_CAST(gst_object_ref(tee)));
        for(MessageProxyPtr& proxyPtr: _waitingPeers) {
            g_signal_emit_by_name(proxyPtr.get(), "tee", tee);
        }
        _waitingPeers.clear();
    }
}

void GstStreamingSource::onTeePadAdded()
{
    if(hasPeers())
        peerAttached();
}

void GstStreamingSource::onTeePadRemoved()
{
    if(!hasPeers()) // only fakesink is linked
        lastPeerDetached();
}

// will be called from streaming thread
void GstStreamingSource::postTeeAvailable(GstElement* tee)
{
    GstBusPtr busPtr(gst_element_get_bus(tee));
    if(!busPtr)
        return;

    GstStructure* structure =
        gst_structure_new_empty("tee");

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(tee), structure);

    gst_bus_post(busPtr.get(), message);
}

// will be called from streaming thread
void GstStreamingSource::postTeePadAdded(GstElement* tee)
{
    GstBusPtr busPtr(gst_element_get_bus(tee));
    if(!busPtr)
        return;

    GstStructure* structure =
        gst_structure_new_empty("tee-pad-added");

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(tee), structure);

    gst_bus_post(busPtr.get(), message);
}

// will be called from streaming thread
void GstStreamingSource::postTeePadRemoved(GstElement* tee)
{
    GstBusPtr busPtr(gst_element_get_bus(tee));
    if(!busPtr)
        return;

    GstStructure* structure =
        gst_structure_new_empty("tee-pad-removed");

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(tee), structure);

    gst_bus_post(busPtr.get(), message);
}

void GstStreamingSource::setTee(GstElementPtr&& teePtr) noexcept
{
    assert(teePtr);
    assert(!_teePtr);

    if(!teePtr || _teePtr)
        return;

    GstElement* tee = teePtr.get();

    auto onPadAddedCallback =
        (void (*) (GstElement*, GstPad*, gpointer*))
        [] (GstElement* tee, GstPad* pad, gpointer*) {
            postTeePadAdded(tee);
        };
    g_signal_connect(tee, "pad-added",
        G_CALLBACK(onPadAddedCallback), pipeline());

    auto onPadRemovedCallback =
        (void (*) (GstElement*, GstPad*, gpointer*))
        [] (GstElement* tee, GstPad* pad, gpointer*) {
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

    postTeeAvailable(tee);
}

GstElement* GstStreamingSource::tee() const noexcept
{
    return _teePtr.get();
}

void GstStreamingSource::cleanup() noexcept
{
    GstElement* pipeline = _pipelinePtr.get();
    if(!pipeline) {
        assert(!_teePtr && !_fakeSinkPtr);
        return;
    }

    stop();

    _teePtr.reset();
    _fakeSinkPtr.reset();

    GstBusPtr busPtr(gst_pipeline_get_bus(GST_PIPELINE(pipeline)));
    gst_bus_remove_watch(busPtr.get());

    _pipelinePtr.reset();
}

void GstStreamingSource::peerAttached() noexcept
{
    GstElement* pipeline = this->pipeline();
    assert(pipeline);
    if(!pipeline)
        return;

    GstState state = GST_STATE_NULL;
    gst_element_get_state(pipeline, &state, nullptr, 0);
    if(state != GST_STATE_PLAYING)
        play();
}

void GstStreamingSource::lastPeerDetached() noexcept
{
}

void GstStreamingSource::peerDestroyed(MessageProxy* messageProxy)
{
    _peers.erase(messageProxy);

    if(_peers.empty())
        cleanup();
}

std::unique_ptr<WebRTCPeer> GstStreamingSource::createPeer() noexcept
{
    if(!prepare())
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
        std::make_unique<GstWebRTCPeer2>(messageProxy);
    if(tee()) {
        g_signal_emit_by_name(messageProxy, "tee", tee());
    } else {
        _waitingPeers.emplace_back(std::move(messageProxyPtr));
    }

    return std::move(peerPtr);
}

void GstStreamingSource::destroyPeer(MessageProxy* messageProxy)
{
    GstBusPtr busPtr(gst_element_get_bus(pipeline()));
    if(!busPtr)
        return;

    GstStructure* structure =
        gst_structure_new(
            "eos",
            "error", G_TYPE_BOOLEAN, true,
            nullptr);

    gst_structure_set(
        structure,
        "target", MESSAGE_PROXY_TYPE, messageProxy,
        NULL);

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(pipeline()), structure);

    gst_bus_post(busPtr.get(), message);
}

void GstStreamingSource::destroyPeers() noexcept
{
    for(MessageProxy* messageProxy: _peers) {
        destroyPeer(messageProxy);
    }
}
