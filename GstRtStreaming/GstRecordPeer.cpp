#include "GstRecordPeer.h"

#include <cassert>
#include <atomic>

#include <netdb.h>
#include <arpa/inet.h>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstWebRtcPtr.h>

#include "Helpers.h"


GstRecordPeer::GstRecordPeer(
    MessageProxy* messageProxy,
    GstElement* pipeline,
    GstElement* rtcbin) :
    _messageProxy(_MESSAGE_PROXY(g_object_ref(messageProxy))),
    _rtcbinPtr(GstElementPtr(GST_ELEMENT(gst_object_ref(rtcbin))))
{
    setPipeline(pipeline);

    auto onMessageCallback =
        + [] (MessageProxy*, GstMessage* message, gpointer userData) {
            GstRecordPeer* owner = static_cast<GstRecordPeer*>(userData);
            return owner->onMessage(message);
        };
    _messageHandlerId = g_signal_connect(
        _messageProxy,
        "message",
        G_CALLBACK(onMessageCallback),
        this);

    auto onEosCallback =
        + [] (MessageProxy*, gboolean error, gpointer userData) {
            GstRecordPeer* owner = static_cast<GstRecordPeer*>(userData);
            return owner->onEos(error);
        };
    _eosHandlerId = g_signal_connect(
        _messageProxy,
        "eos",
        G_CALLBACK(onEosCallback),
        this);
}

GstRecordPeer::~GstRecordPeer()
{
    g_signal_handler_disconnect(_messageProxy, _messageHandlerId);
    g_signal_handler_disconnect(_messageProxy, _eosHandlerId);

    GstElement* rtcbin = webRtcBin();
    g_signal_handler_disconnect(rtcbin, _iceGatheringStateHandlerId);
    g_signal_handler_disconnect(rtcbin, _iceCandidateHandlerId);

    g_object_unref(_messageProxy);
}

void GstRecordPeer::onMessage(GstMessage* message)
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
void GstRecordPeer::postIceCandidate(
    MessageProxy* messageProxy,
    GstElement* rtcbin,
    guint mlineIndex,
    const gchar* candidate)
{
    g_autoptr(GstBus) bus = gst_element_get_bus(rtcbin);
    if(!bus)
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

    gst_bus_post(bus, message);
}

// will be called from streaming thread
void GstRecordPeer::postSdp(
    MessageProxy* messageProxy,
    GstElement* rtcbin,
    const gchar* sdp)
{
    g_autoptr(GstBus) bus = gst_element_get_bus(rtcbin);
    if(!bus)
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

    gst_bus_post(bus, message);
}

// will be called from streaming thread
void GstRecordPeer::postEos(
    MessageProxy* messageProxy,
    GstElement* rtcbin,
    gboolean error)
{
    g_autoptr(GstBus) bus = gst_element_get_bus(rtcbin);
    if(!bus)
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

    gst_bus_post(bus, message);
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
void GstRecordPeer::onAnswerCreated(
    MessageProxy* messageProxy,
    GstElement* rtcbin,
    GstPromise* promise)
{
    GstPromisePtr promisePtr(promise);

    const GstStructure* reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription* sessionDescription = nullptr;
    gst_structure_get(
        reply,
        "answer",
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
        &sessionDescription,
        NULL);
    GstWebRTCSessionDescriptionPtr sessionDescriptionPtr(sessionDescription);

    if(!sessionDescription) {
        postEos(messageProxy, rtcbin, true);
        return;
    }

    g_signal_emit_by_name(
        rtcbin,
        "set-local-description",
        sessionDescription,
        NULL);

    GCharPtr sdpPtr(gst_sdp_message_as_text(sessionDescription->sdp));
    postSdp(messageProxy, rtcbin, sdpPtr.get());
}

// will be called from streaming thread
void GstRecordPeer::onIceGatheringStateChanged(
    MessageProxy* messageProxy,
    GstElement* rtcbin)
{
    GstWebRTCICEGatheringState state = GST_WEBRTC_ICE_GATHERING_STATE_NEW;
    g_object_get(rtcbin, "ice-gathering-state", &state, NULL);

    if(GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE == state)
        postIceCandidate(messageProxy, rtcbin, 0, nullptr);
}

// will be called from streaming thread
void GstRecordPeer::onSetRemoteDescription(
    MessageProxy* messageProxy,
    GstElement* rtcbin,
    GstPromise* promise)
{
    GstPromisePtr promisePtr(promise);

    GstWebRTCSignalingState state = GST_WEBRTC_SIGNALING_STATE_CLOSED;
    g_object_get(rtcbin, "signaling-state", &state, nullptr);

    switch(state) {
    case GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER: {
        auto onAnswerCreatedCallback =
            + [] (GstPromise* promise, gpointer userData) {
                PeerData* peerData = static_cast<PeerData*>(userData);
                return GstRecordPeer::onAnswerCreated(
                    peerData->messageProxy,
                    peerData->rtcBin,
                    promise);
            };

        GstPromise* promise = gst_promise_new_with_change_func(
            onAnswerCreatedCallback,
            new PeerData(messageProxy, rtcbin), &PeerData::destroy);
        g_signal_emit_by_name(rtcbin, "create-answer", nullptr, promise);

        break;
    }
    default:
        GstRecordPeer::postEos(messageProxy, rtcbin, true);
        break;
    }
}

void GstRecordPeer::setRemoteSdp(const std::string& sdp) noexcept
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
            GST_WEBRTC_SDP_TYPE_OFFER,
            sdpMessagePtr.release()));
    GstWebRTCSessionDescription* sessionDescription = sessionDescriptionPtr.get();

    auto onSetRemoteDescriptionCallback =
        + [] (GstPromise* promise, gpointer userData)
    {
        PeerData* data = reinterpret_cast<PeerData*>(userData);
        return GstRecordPeer::onSetRemoteDescription(
            data->messageProxy,
            data->rtcBin,
            promise);
    };

    GstPromise* promise = gst_promise_new_with_change_func(
        onSetRemoteDescriptionCallback,
        new PeerData(_messageProxy, rtcbin),
         &PeerData::destroy);

    g_signal_emit_by_name(rtcbin, "set-remote-description", sessionDescription, promise);
}

void GstRecordPeer::internalPrepare() noexcept
{
    GstElement* pipeline = this->pipeline();
    assert(pipeline);

    GstElement* rtcbin = this->webRtcBin();
    assert(rtcbin);

    if(!IceGatheringStateBroken) {
        auto onIceGatheringStateChangedCallback =
            + [] (GstElement* rtcbin, GParamSpec*, MessageProxy* messageProxy) {
                return GstRecordPeer::onIceGatheringStateChanged(messageProxy, rtcbin);
            };
        _iceGatheringStateHandlerId = g_signal_connect_object(
            rtcbin,
            "notify::ice-gathering-state",
            G_CALLBACK(onIceGatheringStateChangedCallback),
            _messageProxy,
            G_CONNECT_DEFAULT);
    }

    auto onIceCandidateCallback =
        + [] (GstElement* rtcbin, guint candidate, gchar* arg2, MessageProxy* messageProxy) {
            postIceCandidate(messageProxy, rtcbin, candidate, arg2);
        };
    _iceCandidateHandlerId = g_signal_connect_object(
        rtcbin,
        "on-ice-candidate",
        G_CALLBACK(onIceCandidateCallback),
        _messageProxy,
        G_CONNECT_DEFAULT);

    GstCapsPtr capsPtr(gst_caps_from_string("application/x-rtp, media=video"));
    GstWebRTCRTPTransceiver* recvonlyTransceiver = nullptr;
    g_signal_emit_by_name(
        rtcbin, "add-transceiver",
        GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY, capsPtr.get(),
        &recvonlyTransceiver);
    GstWebRTCRTPTransceiverPtr recvonlyTransceiverPtr(recvonlyTransceiver);
}

void GstRecordPeer::prepare(
    const WebRTCConfigPtr& webRTCConfig,
    const PreparedCallback& prepared,
    const IceCandidateCallback& iceCandidate,
    const EosCallback& eos) noexcept
{
    setWebRtcBin(*webRTCConfig, std::move(_rtcbinPtr));
    GstWebRTCPeerBase::attachClient(prepared, iceCandidate, eos);

    assert(pipeline());
    if(!pipeline()) {
        onEos(true);
        return;
    }

    assert(webRtcBin());
    if(!webRtcBin()) {
        onEos(true);
        return;
    }

    internalPrepare();
}
