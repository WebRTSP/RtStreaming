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
    _pipelinePtr(GST_ELEMENT(gst_object_ref(pipeline))),
    _rtcbinPtr(GST_ELEMENT(gst_object_ref(rtcbin)))
{
    auto onMessageCallback =
        (void (*) (MessageProxy*, GstMessage*, gpointer))
        [] (MessageProxy*, GstMessage* message, gpointer userData) {
            GstRecordPeer* owner = static_cast<GstRecordPeer*>(userData);
            return owner->onMessage(message);
        };
    _messageHandlerId =
        g_signal_connect(_messageProxy, "message",
            G_CALLBACK(onMessageCallback), this);

    auto onEosCallback =
        (void (*) (MessageProxy*, gboolean, gpointer))
        [] (MessageProxy*, gboolean error, gpointer userData) {
            GstRecordPeer* owner = static_cast<GstRecordPeer*>(userData);
            return owner->onEos(error);
        };
    _eosHandlerId =
        g_signal_connect(_messageProxy, "eos",
            G_CALLBACK(onEosCallback), this);
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

void GstRecordPeer::onIceCandidate(
    unsigned mlineIndex,
    const gchar* candidate)
{
    if(_iceCandidateCallback) {
        if(!candidate)
            _iceCandidateCallback(mlineIndex, "a=end-of-candidates");
        else
            _iceCandidateCallback(mlineIndex, std::string("a=") + candidate);
    }
}

void GstRecordPeer::onSdp(const gchar* sdp)
{
    _sdp = sdp;

    onPrepared();
}

void GstRecordPeer::onPrepared()
{
    if(_preparedCallback)
        _preparedCallback();
}

void GstRecordPeer::onEos(bool /*error*/)
{
    if(_eosCallback)
        _eosCallback();
}

// will be called from streaming thread
void GstRecordPeer::postIceCandidate(
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
void GstRecordPeer::postSdp(
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
void GstRecordPeer::postEos(
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

GstElement* GstRecordPeer::pipeline() const noexcept
{
    return _pipelinePtr.get();
}

GstElement* GstRecordPeer::webRtcBin() const noexcept
{
    return _rtcbinPtr.get();
}

void GstRecordPeer::setIceServers()
{
    GstElement* rtcbin = webRtcBin();

    guint vMajor = 0, vMinor = 0;
    gst_plugins_base_version(&vMajor, &vMinor, nullptr, nullptr);

    const bool useAddTurnServer =
        vMajor > 1 || (vMajor == 1 && vMinor >= 16);

    for(const std::string& iceServer: _iceServers) {
        using namespace GstRtStreaming;
        switch(ParseIceServerType(iceServer)) {
            case IceServerType::Stun:
                g_object_set(
                    rtcbin,
                    "stun-server", iceServer.c_str(),
                    nullptr);
                break;
            case IceServerType::Turn:
            case IceServerType::Turns: {
                if(useAddTurnServer) {
                    gboolean ret;
                    g_signal_emit_by_name(
                        rtcbin,
                        "add-turn-server", iceServer.c_str(),
                        &ret);
                } else {
                    g_object_set(
                        rtcbin,
                        "turn-server", iceServer.c_str(),
                        nullptr);
                }
                break;
            }
            default:
                break;
        }
    }
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
void GstRecordPeer::onIceGatheringStateChanged(
    MessageProxy* messageProxy,
    GstElement* rtcbin)
{
    GstWebRTCICEGatheringState state = GST_WEBRTC_ICE_GATHERING_STATE_NEW;
    g_object_get(rtcbin, "ice-gathering-state", &state, NULL);

    if(GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE == state) {
        // "ice-gathering-state" is broken in GStreamer < 1.17.1
        guint gstMajor = 0, gstMinor = 0, gstNano = 0;
        gst_plugins_base_version(&gstMajor, &gstMinor, &gstNano, 0);
        if((gstMajor == 1 && gstMinor == 17 && gstNano >= 1) ||
           (gstMajor == 1 && gstMinor > 17) ||
            gstMajor > 1)
        {
            postIceCandidate(messageProxy, rtcbin, 0, nullptr);
        }
    }
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
            (void (*) (GstPromise*, gpointer))
            [] (GstPromise* promise, gpointer userData)
        {
            PeerData* peerData = static_cast<PeerData*>(userData);
            return
                GstRecordPeer::onAnswerCreated(
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
        GstRecordPeer::postEos(messageProxy, rtcbin, true);
        break;
    }
}

void GstRecordPeer::setRemoteSdp(const std::string& sdp) noexcept
{
    GstElement* rtcbin = _rtcbinPtr.get();

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
    GstWebRTCSessionDescription* sessionDescription =
        sessionDescriptionPtr.get();

    auto onSetRemoteDescriptionCallback =
        (void (*) (GstPromise*, gpointer))
        [] (GstPromise* promise, gpointer userData)
    {
        PeerData* data = reinterpret_cast<PeerData*>(userData);
        return
            GstRecordPeer::onSetRemoteDescription(
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

const std::string& GstRecordPeer::sdp() noexcept
{
    return _sdp;
}

void GstRecordPeer::addIceCandidate(
    unsigned mlineIndex,
    const std::string& candidate) noexcept
{
    GstElement* rtcbin = webRtcBin();

    if(candidate.empty() || candidate == "a=end-of-candidates") {
        guint gstMajor = 0, gstMinor = 0;
        gst_plugins_base_version(&gstMajor, &gstMinor, 0, 0);
        if((gstMajor == 1 && gstMinor > 18) || gstMajor > 1) {
            //"end-of-candidates" support was added only after GStreamer 1.18
            g_signal_emit_by_name(
                rtcbin, "add-ice-candidate",
                mlineIndex, 0);
        }

        return;
    }

    if(!candidate.empty()) {
        g_signal_emit_by_name(
            rtcbin, "add-ice-candidate",
            mlineIndex, candidate.c_str());
    } else {
        g_signal_emit_by_name(
            rtcbin, "add-ice-candidate",
            mlineIndex, candidate.data());
    }
}

void GstRecordPeer::internalPrepare() noexcept
{
    GstElement* pipeline = this->pipeline();
    assert(pipeline);

    GstElement* rtcbin = this->webRtcBin();
    assert(rtcbin);

    setIceServers();

    auto onIceGatheringStateChangedCallback =
        (void (*) (GstElement*, GParamSpec* , MessageProxy*))
        [] (GstElement* rtcbin, GParamSpec*, MessageProxy* messageProxy) {
            return GstRecordPeer::onIceGatheringStateChanged(messageProxy, rtcbin);
        };
    _iceGatheringStateHandlerId =
        g_signal_connect_object(rtcbin,
            "notify::ice-gathering-state",
            G_CALLBACK(onIceGatheringStateChangedCallback), _messageProxy, GConnectFlags());

    auto onIceCandidateCallback =
        (void (*) (GstElement*, guint, gchar*, MessageProxy*))
        [] (GstElement* rtcbin, guint candidate, gchar* arg2, MessageProxy* messageProxy) {
            postIceCandidate(messageProxy, rtcbin, candidate, arg2);
        };
    _iceCandidateHandlerId =
        g_signal_connect_object(rtcbin, "on-ice-candidate",
            G_CALLBACK(onIceCandidateCallback), _messageProxy, GConnectFlags());

    GstCapsPtr capsPtr(gst_caps_from_string("application/x-rtp, media=video"));
    GstWebRTCRTPTransceiver* recvonlyTransceiver = nullptr;
    g_signal_emit_by_name(
        rtcbin, "add-transceiver",
        GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY, capsPtr.get(),
        &recvonlyTransceiver);
    GstWebRTCRTPTransceiverPtr recvonlyTransceiverPtr(recvonlyTransceiver);
}

void GstRecordPeer::prepare(
    const IceServers& iceServers,
    const PreparedCallback& prepared,
    const IceCandidateCallback& iceCandidate,
    const EosCallback& eos) noexcept
{
    _iceServers = iceServers;
    _preparedCallback = prepared;
    _iceCandidateCallback = iceCandidate;
    _eosCallback = eos;

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

void GstRecordPeer::play() noexcept
{
}

void GstRecordPeer::stop() noexcept
{
}
