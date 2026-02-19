#include "GstWebRTCPeer.h"

#include <cassert>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstWebRtcPtr.h>


GstWebRTCPeer::GstWebRTCPeer(Role role) noexcept :
    _role(role)
{
}

GstWebRTCPeer::~GstWebRTCPeer() noexcept
{
    stop();

    if(GstElement* pipeline = this->pipeline()) {
        GstBusPtr busPtr(gst_pipeline_get_bus(GST_PIPELINE(pipeline)));
        gst_bus_remove_watch(busPtr.get());
    }
}

void GstWebRTCPeer::setState(GstState state) noexcept
{
    GstElement* pipeline = this->pipeline();

    if(!pipeline) {
        if(state != GST_STATE_NULL)
            ;
        return;
    }

    switch(gst_element_set_state(pipeline, state)) {
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

void GstWebRTCPeer::pause() noexcept
{
    setState(GST_STATE_PAUSED);
}

void GstWebRTCPeer::play() noexcept
{
    setState(GST_STATE_PLAYING);
}

void GstWebRTCPeer::stop() noexcept
{
    setState(GST_STATE_NULL);
}

gboolean GstWebRTCPeer::onBusMessage(GstMessage* message) noexcept
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
        case GST_MESSAGE_LATENCY:
            if(const GstElement* pipeline = this->pipeline()) {
                gst_bin_recalculate_latency(GST_BIN(pipeline));
            }
            break;
        case GST_MESSAGE_APPLICATION: {
            const GstStructure* structure =
                gst_message_get_structure(message);
            assert(structure);
            if(!structure)
                break;

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

// will be called from streaming thread
void GstWebRTCPeer::postIceCandidate(
    GstElement* rtcbin,
    guint mlineIndex,
    const gchar* candidate) noexcept
{
    GValue candidateValue = G_VALUE_INIT;
    g_value_init(&candidateValue, G_TYPE_STRING);
    g_value_take_string(&candidateValue, g_strdup(candidate));

    GstStructure* structure =
        gst_structure_new(
            "ice-candidate",
            "mline-index", G_TYPE_UINT, mlineIndex,
            nullptr);

    gst_structure_take_value(
        structure,
        "candidate",
        &candidateValue);

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(rtcbin), structure);

    g_autoptr(GstBus) bus = gst_element_get_bus(rtcbin);
    gst_bus_post(bus, message);
}

// will be called from streaming thread
void GstWebRTCPeer::postSdp(
    GstElement* rtcbin,
    const gchar* sdp)
{
    GValue sdpValue = G_VALUE_INIT;
    g_value_init(&sdpValue, G_TYPE_STRING);
    g_value_take_string(&sdpValue, g_strdup(sdp));

    GstStructure* structure =
        gst_structure_new_empty("sdp");

    gst_structure_take_value(
        structure,
        "sdp",
        &sdpValue);

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(rtcbin), structure);

    g_autoptr(GstBus) bus = gst_element_get_bus(rtcbin);
    gst_bus_post(bus, message);
}

// will be called from streaming thread
void GstWebRTCPeer::postEos(
    GstElement* rtcbin,
    gboolean error) noexcept
{
    GstStructure* structure =
        gst_structure_new(
            "eos",
            "error", G_TYPE_BOOLEAN, error,
            nullptr);

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(rtcbin), structure);

    g_autoptr(GstBus) bus = gst_element_get_bus(rtcbin);
    gst_bus_post(bus, message);
}

void GstWebRTCPeer::setPipeline(GstElementPtr&& pipelinePtr) noexcept
{
    assert(pipelinePtr && !pipeline());
    if(!pipelinePtr || pipeline())
        return;

    GstWebRTCPeerBase::setPipeline(std::move(pipelinePtr));

    GstElement* pipeline = this->pipeline();

    auto onBusMessageCallback =
        [] (GstBus* bus, GstMessage* message, gpointer userData) -> gboolean {
            GstWebRTCPeer* self = static_cast<GstWebRTCPeer*>(userData);
            return self->onBusMessage(message);
        };
    GstBusPtr busPtr(gst_pipeline_get_bus(GST_PIPELINE(pipeline)));
    gst_bus_add_watch(busPtr.get(), onBusMessageCallback, this);
}

void GstWebRTCPeer::setWebRtcBin(
    const WebRTCConfig& webRTCConfig,
    GstElementPtr&& rtcbinPtr) noexcept
{
    assert(rtcbinPtr && !webRtcBin());
    if(!rtcbinPtr || webRtcBin())
        return;

   GstWebRTCPeerBase::setWebRtcBin(webRTCConfig, std::move(rtcbinPtr));

    GstElement* rtcbin = webRtcBin();

    if(Role::Streamer == _role) {
        auto onNegotiationNeededCallback =
            + [] (GstElement* rtcbin, gpointer userData) {
                GstWebRTCPeer* owner = static_cast<GstWebRTCPeer*>(userData);
                assert(rtcbin == owner->webRtcBin());
                return GstWebRTCPeer::onNegotiationNeeded(rtcbin);
            };
        g_signal_connect(
            rtcbin,
            "on-negotiation-needed",
            G_CALLBACK(onNegotiationNeededCallback),
            this);
    }

    if(!IceGatheringStateBroken) {
        auto onIceGatheringStateChangedCallback =
            + [] (GstElement* rtcbin, GParamSpec*, gpointer userData) {
                return GstWebRTCPeer::onIceGatheringStateChanged(rtcbin);
            };
        g_signal_connect(
            rtcbin,
            "notify::ice-gathering-state",
            G_CALLBACK(onIceGatheringStateChangedCallback),
            nullptr);
    }

    auto onIceCandidateCallback =
        + [] (GstElement* rtcbin, guint mlineIndex, gchar* candidate, gpointer userData) {
            GstWebRTCPeer* self = static_cast<GstWebRTCPeer*>(userData);
            assert(rtcbin == self->webRtcBin());
            postIceCandidate(rtcbin, mlineIndex, candidate);
        };
    g_signal_connect(
        rtcbin,
        "on-ice-candidate",
        G_CALLBACK(onIceCandidateCallback),
        this);

    if(Role::Streamer == _role) {
        GArray* transceivers;
        g_signal_emit_by_name(rtcbin, "get-transceivers", &transceivers);
        for(guint i = 0; i < transceivers->len; ++i) {
            GstWebRTCRTPTransceiver* transceiver = g_array_index(transceivers, GstWebRTCRTPTransceiver*, i);
#if GST_CHECK_VERSION(1, 18, 0)
            g_object_set(transceiver, "direction", GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY, nullptr);
#else
            transceiver->direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
#endif
        }
        g_array_unref(transceivers);
    }
}

// will be called from streaming thread
void GstWebRTCPeer::onOfferCreated(
    GstElement* rtcbin,
    GstPromise* promise) noexcept
{
    GstPromisePtr promisePtr(promise);

    const GstStructure* reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription* sessionDescription = nullptr;
    gst_structure_get(
        reply,
        "offer",
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
        &sessionDescription,
        NULL);
    GstWebRTCSessionDescriptionPtr sessionDescriptionPtr(sessionDescription);

    g_signal_emit_by_name(rtcbin, "set-local-description", sessionDescription, NULL);

    GCharPtr sdpPtr(gst_sdp_message_as_text(sessionDescription->sdp));
    postSdp(rtcbin, sdpPtr.get());
}

// will be called from streaming thread
void GstWebRTCPeer::onNegotiationNeeded(GstElement* rtcbin) noexcept
{
    auto onOfferCreatedCallback =
        + [] (GstPromise* promise, gpointer userData) {
            GstElement* rtcbin = static_cast<GstElement*>(userData);
            return GstWebRTCPeer::onOfferCreated(rtcbin, promise);
        };

    GstPromise* promise = gst_promise_new_with_change_func(
        onOfferCreatedCallback,
        rtcbin,
        nullptr);
    g_signal_emit_by_name(
        rtcbin, "create-offer", nullptr, promise);
}

// will be called from streaming thread
void GstWebRTCPeer::onAnswerCreated(
    GstElement* rtcbin,
    GstPromise* promise) noexcept
{
    GstPromisePtr promisePtr(promise);

    const GstStructure* reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription* sessionDescription = nullptr;
    gst_structure_get(reply, "answer",
        GST_TYPE_WEBRTC_SESSION_DESCRIPTION,
        &sessionDescription, NULL);
    GstWebRTCSessionDescriptionPtr sessionDescriptionPtr(sessionDescription);

    if(!sessionDescription) {
        postEos(rtcbin, true);
        return;
    }

    g_signal_emit_by_name(rtcbin,
        "set-local-description", sessionDescription, NULL);

    GCharPtr sdpPtr(gst_sdp_message_as_text(sessionDescription->sdp));
    postSdp(rtcbin, sdpPtr.get());
}

// will be called from streaming thread
void GstWebRTCPeer::onIceGatheringStateChanged(GstElement* rtcbin) noexcept
{
    GstWebRTCICEGatheringState state = GST_WEBRTC_ICE_GATHERING_STATE_NEW;
    g_object_get(rtcbin, "ice-gathering-state", &state, NULL);

    if(GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE == state)
        postIceCandidate(rtcbin, 0, nullptr);
}

// will be called from streaming thread
void GstWebRTCPeer::onSetRemoteDescription(
    GstElement* rtcbin,
    GstPromise* promise) noexcept
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
            + [] (GstPromise* promise, gpointer userData) {
                GstElement* rtcbin = static_cast<GstElement*>(userData);
                return GstWebRTCPeer::onAnswerCreated(rtcbin, promise);
            };

        GstPromise* promise = gst_promise_new_with_change_func(
            onAnswerCreatedCallback,
            rtcbin,
            nullptr);
        g_signal_emit_by_name(rtcbin, "create-answer", nullptr, promise);

        break;
    }
    default:
        GstWebRTCPeer::postEos(rtcbin, true);
        break;
    }
}

void GstWebRTCPeer::setRemoteSdp(const std::string& sdp) noexcept
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
            Role::Viewer == _role  ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER,
            sdpMessagePtr.release()));
    GstWebRTCSessionDescription* sessionDescription =
        sessionDescriptionPtr.get();

    auto onSetRemoteDescriptionCallback =
        + [] (GstPromise* promise, gpointer userData)
    {
        GstElement* rtcbin = static_cast<GstElement*>(userData);
        return GstWebRTCPeer::onSetRemoteDescription(rtcbin, promise);
    };

    GstPromise* promise = gst_promise_new_with_change_func(
        onSetRemoteDescriptionCallback,
        rtcbin,
        nullptr);

    g_signal_emit_by_name(
        rtcbin,
        "set-remote-description",
        sessionDescription,
        promise);
}

void GstWebRTCPeer::prepare(
    const WebRTCConfigPtr& webRTCConfig,
    const PreparedCallback& prepared,
    const IceCandidateCallback& iceCandidate,
    const EosCallback& eos,
    const std::string& logContext) noexcept
{
    assert(!pipeline());
    if(pipeline())
        return;

    GstWebRTCPeerBase::attachClient(prepared, iceCandidate, eos, logContext);

    prepare(webRTCConfig);

    if(!pipeline())
        onEos(true);
}
