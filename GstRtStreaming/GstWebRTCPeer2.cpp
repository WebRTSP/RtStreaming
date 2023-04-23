#include "GstWebRTCPeer2.h"

#include <cassert>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstWebRtcPtr.h>

#include "Helpers.h"


GstWebRTCPeer2::GstWebRTCPeer2(MessageProxy* messageProxy) :
    _messageProxy(_MESSAGE_PROXY(g_object_ref(messageProxy)))
{
    auto onTeeCallback =
        (void (*) (MessageProxy*, GstElement*, gpointer))
        [] (MessageProxy*, GstElement* tee, gpointer userData) {
            GstWebRTCPeer2* owner = static_cast<GstWebRTCPeer2*>(userData);
            assert(!owner->pipeline() && !owner->_teePtr);
            owner->setPipeline(GstElementPtr(GST_ELEMENT(gst_object_get_parent(GST_OBJECT(tee)))));
            owner->_teePtr.reset(GST_ELEMENT(g_object_ref(tee))),
            owner->internalPrepare();
        };
    _teeHandlerId =
        g_signal_connect(_messageProxy, "tee",
            G_CALLBACK(onTeeCallback), this);

    auto onMessageCallback =
        (void (*) (MessageProxy*, GstMessage*, gpointer))
        [] (MessageProxy*, GstMessage* message, gpointer userData) {
            GstWebRTCPeer2* owner = static_cast<GstWebRTCPeer2*>(userData);
            return owner->onMessage(message);
        };
    _messageHandlerId =
        g_signal_connect(_messageProxy, "message",
            G_CALLBACK(onMessageCallback), this);

    auto onEosCallback =
        (void (*) (MessageProxy*, gboolean, gpointer))
        [] (MessageProxy*, gboolean error, gpointer userData) {
            GstWebRTCPeer2* owner = static_cast<GstWebRTCPeer2*>(userData);
            return owner->onEos(error);
        };
    _eosHandlerId =
        g_signal_connect(_messageProxy, "eos",
            G_CALLBACK(onEosCallback), this);
}

namespace {

struct TeardownData
{
    std::atomic_flag guard = ATOMIC_FLAG_INIT;

    GstElementPtr pipelinePtr;
    GstElementPtr teePtr;
    GstElementPtr queuePtr;
    GstElementPtr rtcbinPtr;
};

GstPadProbeReturn
RemovePeerElements(
    GstPad* pad,
    GstPadProbeInfo*,
    gpointer userData)
{
    TeardownData* data = static_cast<TeardownData*>(userData);

    if(data->guard.test_and_set())
        return GST_PAD_PROBE_OK;

    GstElement* pipeline = data->pipelinePtr.get();
    GstElement* tee = data->teePtr.get();
    GstElement* queue = data->queuePtr.get();
    GstElement* rtcbin = data->rtcbinPtr.get();

    GstPadPtr queueSinkPadPtr(gst_element_get_static_pad(queue, "sink"));
    GstPadPtr teeSrcPadPtr(gst_pad_get_peer(queueSinkPadPtr.get()));

    gst_pad_unlink(teeSrcPadPtr.get(), queueSinkPadPtr.get());
    gst_element_release_request_pad(tee, teeSrcPadPtr.get());

    GstPadPtr queueSrcPadPtr(gst_element_get_static_pad(queue, "src"));
    GstPadPtr rtcbinSinkPadPtr(gst_pad_get_peer(queueSrcPadPtr.get()));

    gst_pad_unlink(queueSrcPadPtr.get(), rtcbinSinkPadPtr.get());
    gst_element_release_request_pad(rtcbin, rtcbinSinkPadPtr.get());

    gst_bin_remove_many(GST_BIN(pipeline), queue, rtcbin, NULL);

    gst_element_set_state(queue, GST_STATE_NULL);
    gst_element_set_state(rtcbin, GST_STATE_NULL);

    return GST_PAD_PROBE_REMOVE;
}

}

GstWebRTCPeer2::~GstWebRTCPeer2()
{
    g_signal_handler_disconnect(_messageProxy, _teeHandlerId);
    g_signal_handler_disconnect(_messageProxy, _messageHandlerId);
    g_signal_handler_disconnect(_messageProxy, _eosHandlerId);

    g_object_unref(_messageProxy);

    if(!_teePtr)
        return; // nothing to teardown

    GstPadPtr teeSinkPadPtr(gst_element_get_static_pad(_teePtr.get(), "sink"));
    GstPadPtr teePeerSrcPadPtr(gst_pad_get_peer(teeSinkPadPtr.get()));

    if(!_prepared.test_and_set()) {
        log()->warn("Peer was not prepared. Skipping teardown...");
        g_assert(_prepareProbe);
        gst_pad_remove_probe(teePeerSrcPadPtr.get(), _prepareProbe);
    } else {
        log()->debug("Teardown peer...");
        TeardownData* data = new TeardownData {
            .pipelinePtr = GstElementPtr(GST_ELEMENT(gst_object_ref(pipeline()))),
            .teePtr = std::move(_teePtr),
            .queuePtr = std::move(_queuePtr),
            .rtcbinPtr = GstElementPtr(GST_ELEMENT(gst_object_ref(webRtcBin()))),
        };

        GstPadPtr queueSinkPadPtr(gst_element_get_static_pad(data->queuePtr.get(), "sink"));
        GstPadPtr teeSrcPadPtr(gst_pad_get_peer(queueSinkPadPtr.get()));

        gst_pad_add_probe(
            teeSrcPadPtr.get(),
            GST_PAD_PROBE_TYPE_IDLE,
            RemovePeerElements,
            data,
            [] (void* userData) { delete static_cast<TeardownData*>(userData); });
    }

}

void GstWebRTCPeer2::onMessage(GstMessage* message)
{
    const GstStructure* structure =
        gst_message_get_structure(message);
    assert(structure);
    if(!structure)
        return;

    if(gst_message_has_name(message, "ice-candidate")) {
        guint mlineIndex = 0;
        gst_structure_get_uint(structure, "mline-index", &mlineIndex);
        const gchar* candidate = gst_structure_get_string(structure, "candidate");
        onIceCandidate(mlineIndex, candidate);
    } else if(gst_message_has_name(message, "sdp")) {
        const gchar* sdp = gst_structure_get_string(structure, "sdp");
        onSdp(sdp);
    } else if(gst_message_has_name(message, "eos")) {
        gboolean error = FALSE;
        gst_structure_get_boolean(structure, "error", &error);
        onEos(error != FALSE);
    }
}

// will be called from streaming thread
void GstWebRTCPeer2::postIceCandidate(
    MessageProxy* messageProxy,
    GstElement* rtcbin,
    guint mlineIndex,
    const gchar* candidate)
{
    GstBusPtr busPtr(gst_element_get_bus(rtcbin));
    if(!busPtr)
        return;

    GValue candidateValue = G_VALUE_INIT;
    g_value_init(&candidateValue, G_TYPE_STRING);
    g_value_take_string(&candidateValue, g_strdup(candidate));

    GstStructure* structure =
        gst_structure_new(
            "ice-candidate",
            "mline-index", G_TYPE_UINT, mlineIndex,
            nullptr);

    gst_structure_set(
        structure,
        "target", MESSAGE_PROXY_TYPE, messageProxy,
        NULL);

    gst_structure_take_value(
        structure,
        "candidate",
        &candidateValue);

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(rtcbin), structure);

    gst_bus_post(busPtr.get(), message);
}

// will be called from streaming thread
void GstWebRTCPeer2::postSdp(
    MessageProxy* messageProxy,
    GstElement* rtcbin,
    const gchar* sdp)
{
    GstBusPtr busPtr(gst_element_get_bus(rtcbin));
    if(!busPtr)
        return;

    GValue sdpValue = G_VALUE_INIT;
    g_value_init(&sdpValue, G_TYPE_STRING);
    g_value_take_string(&sdpValue, g_strdup(sdp));

    GstStructure* structure =
        gst_structure_new_empty("sdp");

    gst_structure_set(
        structure,
        "target", MESSAGE_PROXY_TYPE, messageProxy,
        NULL);

    gst_structure_take_value(
        structure,
        "sdp",
        &sdpValue);

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(rtcbin), structure);

    gst_bus_post(busPtr.get(), message);
}

// will be called from streaming thread
void GstWebRTCPeer2::postEos(
    MessageProxy* messageProxy,
    GstElement* rtcbin,
    gboolean error)
{
    GstBusPtr busPtr(gst_element_get_bus(rtcbin));
    if(!busPtr)
        return;

    GstStructure* structure =
        gst_structure_new(
            "eos",
            "error", G_TYPE_BOOLEAN, error,
            nullptr);

    gst_structure_set(
        structure,
        "target", MESSAGE_PROXY_TYPE, messageProxy,
        NULL);

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(rtcbin), structure);

    gst_bus_post(busPtr.get(), message);
}

GstElement* GstWebRTCPeer2::tee() const noexcept
{
    return _teePtr.get();
}

GstElement* GstWebRTCPeer2::queue() const noexcept
{
    return _queuePtr.get();
}

void GstWebRTCPeer2::prepareWebRtcBin() noexcept
{
    GstElement* rtcbin = webRtcBin();

    assert(rtcbin);
    if(!rtcbin)
        return;

    auto onNegotiationNeededCallback =
        (void (*) (GstElement*, MessageProxy*))
        [] (GstElement* rtcbin, MessageProxy* messageProxy) {
            return GstWebRTCPeer2::onNegotiationNeeded(messageProxy, rtcbin);
        };
    g_signal_connect_object(rtcbin, "on-negotiation-needed",
        G_CALLBACK(onNegotiationNeededCallback), _messageProxy, GConnectFlags());

    if(!IceGatheringStateBroken) {
        auto onIceGatheringStateChangedCallback =
            (void (*) (GstElement*, GParamSpec* , MessageProxy*))
            [] (GstElement* rtcbin, GParamSpec*, MessageProxy* messageProxy) {
                return GstWebRTCPeer2::onIceGatheringStateChanged(messageProxy, rtcbin);
            };
        g_signal_connect_object(rtcbin,
            "notify::ice-gathering-state",
            G_CALLBACK(onIceGatheringStateChangedCallback), _messageProxy, GConnectFlags());
    }

    auto onIceCandidateCallback =
        (void (*) (GstElement*, guint, gchar*, MessageProxy*))
        [] (GstElement* rtcbin, guint candidate, gchar* arg2, MessageProxy* messageProxy) {
            postIceCandidate(messageProxy, rtcbin, candidate, arg2);
        };
    g_signal_connect_object(rtcbin, "on-ice-candidate",
        G_CALLBACK(onIceCandidateCallback), _messageProxy, GConnectFlags());

    GArray* transceivers;
    g_signal_emit_by_name(rtcbin, "get-transceivers", &transceivers);
    for(guint i = 0; i < transceivers->len; ++i) {
        GstWebRTCRTPTransceiver* transceiver = g_array_index(transceivers, GstWebRTCRTPTransceiver*, 0);
#if GST_CHECK_VERSION(1, 18, 0)
        g_object_set(transceiver, "direction", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY, nullptr);
#else
        transceiver->direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
#endif
    }
    g_array_unref(transceivers);
}

// will be called from streaming thread
void GstWebRTCPeer2::onOfferCreated(
    MessageProxy* messageProxy,
    GstElement* rtcbin,
    GstPromise* promise)
{
    GstPromisePtr promisePtr(promise);

    const GstStructure* reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription* sessionDescription = nullptr;
    gst_structure_get(reply, "offer",
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
        &sessionDescription, NULL);
    GstWebRTCSessionDescriptionPtr sessionDescriptionPtr(sessionDescription);

    g_signal_emit_by_name(rtcbin,
        "set-local-description", sessionDescription, NULL);

    GCharPtr sdpPtr(gst_sdp_message_as_text(sessionDescription->sdp));
    postSdp(messageProxy, rtcbin, sdpPtr.get());
}

namespace {

struct PeerData
{
    PeerData(MessageProxy* messageProxy, GstElement* rtcBin) :
        messageProxy(static_cast<MessageProxy*>(g_object_ref(messageProxy))),
        rtcBin(rtcBin) {}
    ~PeerData() {
        g_object_unref(messageProxy);
    }

    static void destroy(void* data) {
        delete reinterpret_cast<PeerData*>(data);
    }

    MessageProxy* messageProxy;
    GstElement* rtcBin;
};

}

// will be called from streaming thread
void GstWebRTCPeer2::onNegotiationNeeded(
    MessageProxy* messageProxy,
    GstElement* rtcbin)
{
    auto onOfferCreatedCallback =
        (void (*) (GstPromise*, gpointer))
        [] (GstPromise* promise, gpointer userData)
    {
        PeerData* data = reinterpret_cast<PeerData*>(userData);
        GstWebRTCPeer2::onOfferCreated(data->messageProxy, data->rtcBin, promise);
    };

    GstPromise* promise =
        gst_promise_new_with_change_func(
            onOfferCreatedCallback,
            new PeerData(messageProxy, rtcbin),
            &PeerData::destroy);
    g_signal_emit_by_name(
        rtcbin, "create-offer", nullptr, promise);
}

// will be called from streaming thread
void GstWebRTCPeer2::onAnswerCreated(
    MessageProxy* messageProxy,
    GstElement* rtcbin,
    GstPromise* promise)
{
    GstPromisePtr promisePtr(promise);

    const GstStructure* reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription* sessionDescription = nullptr;
    gst_structure_get(reply, "answer",
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
        &sessionDescription, NULL);
    GstWebRTCSessionDescriptionPtr sessionDescriptionPtr(sessionDescription);

    if(!sessionDescription) {
        postEos(messageProxy, rtcbin, true);
        return;
    }

    g_signal_emit_by_name(rtcbin,
        "set-local-description", sessionDescription, NULL);

    GCharPtr sdpPtr(gst_sdp_message_as_text(sessionDescription->sdp));
    postSdp(messageProxy, rtcbin, sdpPtr.get());
}

// will be called from streaming thread
void GstWebRTCPeer2::onIceGatheringStateChanged(
    MessageProxy* messageProxy,
    GstElement* rtcbin)
{
    GstWebRTCICEGatheringState state = GST_WEBRTC_ICE_GATHERING_STATE_NEW;
    g_object_get(rtcbin, "ice-gathering-state", &state, NULL);

    if(GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE == state)
        postIceCandidate(messageProxy, rtcbin, 0, nullptr);
}

// will be called from streaming thread
void GstWebRTCPeer2::onSetRemoteDescription(
    MessageProxy* messageProxy,
    GstElement* rtcbin,
    GstPromise* promise)
{
    GstPromisePtr promisePtr(promise);

    GstWebRTCSignalingState state = GST_WEBRTC_SIGNALING_STATE_CLOSED;
    g_object_get(rtcbin, "signaling-state", &state, nullptr);

    switch(state) {
    case GST_WEBRTC_SIGNALING_STATE_STABLE:
        break;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_OFFER:
        break;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER: {
        auto onAnswerCreatedCallback =
            (void (*) (GstPromise*, gpointer))
            [] (GstPromise* promise, gpointer userData)
        {
            PeerData* peerData = static_cast<PeerData*>(userData);
            return
                GstWebRTCPeer2::onAnswerCreated(
                    peerData->messageProxy,
                    peerData->rtcBin,
                    promise);
        };

        GstPromise* promise =
            gst_promise_new_with_change_func(
                onAnswerCreatedCallback,
                new PeerData(messageProxy, rtcbin), &PeerData::destroy);
        g_signal_emit_by_name(
            rtcbin, "create-answer", nullptr, promise);

        break;
    }
    default:
        GstWebRTCPeer2::postEos(messageProxy, rtcbin, true);
        break;
    }
}

void GstWebRTCPeer2::setRemoteSdp(const std::string& sdp) noexcept
{
    GstElement* rtcbin = webRtcBin();

    GstSDPMessage* sdpMessage;
    gst_sdp_message_new(&sdpMessage);
    GstSDPMessagePtr sdpMessagePtr(sdpMessage);
    gst_sdp_message_parse_buffer(
        reinterpret_cast<const guint8*>(sdp.data()),
        sdp.size(),
        sdpMessage);

    GstWebRTCSessionDescriptionPtr sessionDescriptionPtr(
        gst_webrtc_session_description_new(
            GST_WEBRTC_SDP_TYPE_ANSWER,
            sdpMessagePtr.release()));
    GstWebRTCSessionDescription* sessionDescription =
        sessionDescriptionPtr.get();

    auto onSetRemoteDescriptionCallback =
        (void (*) (GstPromise*, gpointer))
        [] (GstPromise* promise, gpointer userData)
    {
        PeerData* data = reinterpret_cast<PeerData*>(userData);
        return
            GstWebRTCPeer2::onSetRemoteDescription(
                data->messageProxy,
                data->rtcBin,
                promise);
    };

    GstPromise* promise =
        gst_promise_new_with_change_func(
            onSetRemoteDescriptionCallback,
            new PeerData(_messageProxy, rtcbin),
             &PeerData::destroy);

    g_signal_emit_by_name(rtcbin,
        "set-remote-description", sessionDescription, promise);
}

namespace {

struct PrepareData {
    std::atomic_flag& guard;

    GstElementPtr pipelinePtr;
    GstElementPtr teePtr;
    GstElementPtr queuePtr;
    GstElementPtr rtcBinPtr;
};

}

void GstWebRTCPeer2::internalPrepare() noexcept
{
    if(!clientAttached()) {
        return;
    }

    GstElement* pipeline = this->pipeline();
    GstElement* tee = this->tee();

    if(!pipeline || !tee) {
        return;
    }

    assert(!webRtcBin());
    if(webRtcBin()) {
        onEos(true);
        return;
    }

    _queuePtr.reset(gst_element_factory_make("queue", nullptr));
    GstElement* queue = _queuePtr.get();
    g_object_set(queue, "silent", true, nullptr);
    gst_util_set_object_arg(G_OBJECT(queue), "leaky", "downstream");

    setWebRtcBin(GstElementPtr(gst_element_factory_make("webrtcbin", nullptr)));
    GstElement* rtcbin = webRtcBin();
    prepareWebRtcBin();

    GstPadPtr teeSinkPadPtr(gst_element_get_static_pad(tee, "sink"));
    GstPadPtr teePeerSrcPadPtr(gst_pad_get_peer(teeSinkPadPtr.get()));

    PrepareData* prepareData =
        new PrepareData {
            _prepared,
            GstElementPtr(GST_ELEMENT(gst_object_ref(pipeline))),
            GstElementPtr(GST_ELEMENT(gst_object_ref(tee))),
            GstElementPtr(GST_ELEMENT(gst_object_ref(queue))),
            GstElementPtr(GST_ELEMENT(gst_object_ref(rtcbin)))
        };

    _prepareProbe = gst_pad_add_probe(
        teePeerSrcPadPtr.get(),
        GST_PAD_PROBE_TYPE_IDLE,
        [] (GstPad*, GstPadProbeInfo*, gpointer data) -> GstPadProbeReturn {
            PrepareData* prepareData = static_cast<PrepareData*>(data);
            GstElement* pipeline = prepareData->pipelinePtr.get();
            GstElement* tee = prepareData->teePtr.get();
            GstElement* queue = prepareData->queuePtr.get();
            GstElement* rtcbin = prepareData->rtcBinPtr.get();

            if(prepareData->guard.test_and_set())
                return GST_PAD_PROBE_OK;

            gst_bin_add_many(
                GST_BIN(pipeline),
                GST_ELEMENT(gst_object_ref(queue)),
                GST_ELEMENT(gst_object_ref(rtcbin)),
                nullptr);

            GstPadPtr teeSrcPadPtr(gst_element_get_request_pad(tee, "src_%u"));
            GstPadPtr queueSinkPadPtr(gst_element_get_static_pad(queue, "sink"));

            if(GST_PAD_LINK_OK != gst_pad_link(teeSrcPadPtr.get(), queueSinkPadPtr.get())) {
                g_assert(false);
            }

            GstPadPtr queueSrcPadPtr(gst_element_get_static_pad(queue, "src"));
            GstPadPtr rtcbinSinkPadPtr(gst_element_get_request_pad(rtcbin, "sink_%u"));

            if(GST_PAD_LINK_OK != gst_pad_link(queueSrcPadPtr.get(), rtcbinSinkPadPtr.get())) {
                g_assert(false);
            }

            if(!gst_element_sync_state_with_parent(queue)) {
                g_assert(false);
            }

            if(!gst_element_sync_state_with_parent(rtcbin)) {
                g_assert(false);
            }

            return GST_PAD_PROBE_REMOVE;
        },
        prepareData,
        [] (gpointer data) { delete(static_cast<PrepareData*>(data)); });
}

void GstWebRTCPeer2::prepare(
    const IceServers& iceServers,
    const PreparedCallback& prepared,
    const IceCandidateCallback& iceCandidate,
    const EosCallback& eos) noexcept
{
    GstWebRTCPeerBase::setIceServers(iceServers);
    GstWebRTCPeerBase::attachClient(prepared, iceCandidate, eos);

    internalPrepare();
}
