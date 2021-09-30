#include "GstWebRTCPeer.h"

#include <cassert>

#include <netdb.h>
#include <arpa/inet.h>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>

#include <CxxPtr/GlibPtr.h>

#include "Helpers.h"


void GstWebRTCPeer::ResolveIceCandidate(
    const std::string& candidate,
    std::string* resolvedCandidate)
{
    if(!resolvedCandidate)
        return;

    resolvedCandidate->clear();

    if(candidate.empty())
        return;

    enum {
        ConnectionAddressPos = 4,
        PortPos = 5,
    };

    std::string::size_type pos = 0;
    unsigned token = 0;

    std::string::size_type connectionAddressPos = std::string::npos;
    std::string::size_type portPos = std::string::npos;
    while(std::string::npos == portPos &&
        (pos = candidate.find(' ', pos)) != std::string::npos)
    {
        ++pos;
        ++token;

        switch(token) {
        case ConnectionAddressPos:
            connectionAddressPos = pos;
            break;
        case PortPos:
            portPos = pos;
            break;
        }
    }

    if(connectionAddressPos != std::string::npos &&
        portPos != std::string::npos)
    {
        std::string connectionAddress(
            candidate, connectionAddressPos,
            portPos - connectionAddressPos - 1);

        const std::string::value_type localSuffix[] = ".local";
        if(connectionAddress.size() > (sizeof(localSuffix) - 1) &&
           connectionAddress.compare(
               connectionAddress.size() - (sizeof(localSuffix) - 1),
               std::string::npos,
               localSuffix) == 0)
        {
            if(hostent* host = gethostbyname(connectionAddress.c_str())) {
                int ipLen;
                switch(host->h_addrtype) {
                case AF_INET:
                    ipLen = INET_ADDRSTRLEN;
                    break;
                case AF_INET6:
                    ipLen = INET6_ADDRSTRLEN;
                    break;
                default:
                    return;
                }

                char ip[ipLen];
                if(nullptr == inet_ntop(
                    host->h_addrtype,
                    host->h_addr_list[0],
                    ip, ipLen))
                {
                   return;
                }

                if(resolvedCandidate) {
                    ipLen = strlen(ip);

                    *resolvedCandidate = candidate.substr(0, connectionAddressPos);
                    resolvedCandidate->append(ip, ipLen);
                    resolvedCandidate->append(
                        candidate,
                        portPos - 1,
                        std::string::npos);
                }
            }
        }
    }
}


GstWebRTCPeer::GstWebRTCPeer(Role role) :
    _role(role)
{
}

GstWebRTCPeer::~GstWebRTCPeer()
{
    stop();

    if(GstElement* pipeline = _pipelinePtr.get()) {
        GstBusPtr busPtr(gst_pipeline_get_bus(GST_PIPELINE(pipeline)));
        gst_bus_remove_watch(busPtr.get());
    }
}

void GstWebRTCPeer::setState(GstState state) noexcept
{
    if(!_pipelinePtr) {
        if(state != GST_STATE_NULL)
            ;
        return;
    }

    GstElement* pipeline = _pipelinePtr.get();

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

gboolean GstWebRTCPeer::onBusMessage(GstMessage* message)
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
            break;
        }
        default:
            break;
    }

    return TRUE;
}

void GstWebRTCPeer::onIceCandidate(
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

void GstWebRTCPeer::onSdp(const gchar* sdp)
{
    _sdp = sdp;

    onPrepared();
}

void GstWebRTCPeer::onPrepared()
{
    if(_preparedCallback)
        _preparedCallback();
}

void GstWebRTCPeer::onEos(bool /*error*/)
{
    if(_eosCallback)
        _eosCallback();
}


// will be called from streaming thread
void GstWebRTCPeer::postIceCandidate(
    GstElement* rtcbin,
    guint mlineIndex,
    const gchar* candidate)
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

    GstBusPtr busPtr(gst_element_get_bus(rtcbin));
    gst_bus_post(busPtr.get(), message);
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

    GstBusPtr busPtr(gst_element_get_bus(rtcbin));
    gst_bus_post(busPtr.get(), message);
}

// will be called from streaming thread
void GstWebRTCPeer::postEos(
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

void GstWebRTCPeer::setPipeline(GstElementPtr&& pipelinePtr) noexcept
{
    assert(pipelinePtr);
    assert(!_pipelinePtr);

    if(!pipelinePtr || _pipelinePtr)
        return;

    _pipelinePtr = std::move(pipelinePtr);
    GstElement* pipeline = this->pipeline();

    auto onBusMessageCallback =
        (gboolean (*) (GstBus*, GstMessage*, gpointer))
        [] (GstBus* bus, GstMessage* message, gpointer userData) -> gboolean
        {
            GstWebRTCPeer* self = static_cast<GstWebRTCPeer*>(userData);
            return self->onBusMessage(message);
        };
    GstBusPtr busPtr(gst_pipeline_get_bus(GST_PIPELINE(pipeline)));
    gst_bus_add_watch(busPtr.get(), onBusMessageCallback, this);
}

GstElement* GstWebRTCPeer::pipeline() const noexcept
{
    return _pipelinePtr.get();
}

void GstWebRTCPeer::setWebRtcBin(GstElementPtr&& rtcbinPtr) noexcept
{
    assert(rtcbinPtr && !_rtcbinPtr);

    if(!rtcbinPtr || _rtcbinPtr)
        return;

    _rtcbinPtr = std::move(rtcbinPtr);

    GstElement* rtcbin = webRtcBin();

    if(Role::Streamer == _role) {
        auto onNegotiationNeededCallback =
            (void (*) (GstElement*, gpointer))
            [] (GstElement* rtcbin, gpointer userData)
            {
                GstWebRTCPeer* owner = static_cast<GstWebRTCPeer*>(userData);
                assert(rtcbin == owner->webRtcBin());
                return GstWebRTCPeer::onNegotiationNeeded(rtcbin);
            };
        g_signal_connect(rtcbin, "on-negotiation-needed",
            G_CALLBACK(onNegotiationNeededCallback), this);
    }

    auto onIceGatheringStateChangedCallback =
        (void (*) (GstElement*, GParamSpec* , gpointer))
        [] (GstElement* rtcbin, GParamSpec*, gpointer userData)
    {
        return GstWebRTCPeer::onIceGatheringStateChanged(rtcbin);
    };
    g_signal_connect(rtcbin,
        "notify::ice-gathering-state",
        G_CALLBACK(onIceGatheringStateChangedCallback),
        nullptr);

    auto onIceCandidateCallback =
        (void (*) (GstElement*, guint, gchar*, gpointer))
        [] (GstElement* rtcbin, guint candidate, gchar* arg2, gpointer userData)
        {
            GstWebRTCPeer* self = static_cast<GstWebRTCPeer*>(userData);
            assert(rtcbin == self->webRtcBin());
            postIceCandidate(rtcbin, candidate, arg2);
        };
    g_signal_connect(rtcbin, "on-ice-candidate",
        G_CALLBACK(onIceCandidateCallback), this);

    if(Role::Streamer == _role) {
        GArray* transceivers;
        g_signal_emit_by_name(rtcbin, "get-transceivers", &transceivers);
        for(guint i = 0; i < transceivers->len; ++i) {
            GstWebRTCRTPTransceiver* transceiver = g_array_index(transceivers, GstWebRTCRTPTransceiver*, 0);
            transceiver->direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
        }
        g_array_unref(transceivers);
    }
}

GstElement* GstWebRTCPeer::webRtcBin() const noexcept
{
    return _rtcbinPtr.get();
}

void GstWebRTCPeer::setIceServers()
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

// will be called from streaming thread
void GstWebRTCPeer::onOfferCreated(
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
    postSdp(rtcbin, sdpPtr.get());
}

// will be called from streaming thread
void GstWebRTCPeer::onNegotiationNeeded(GstElement* rtcbin)
{
    auto onOfferCreatedCallback =
        (void (*) (GstPromise*, gpointer))
        [] (GstPromise* promise, gpointer userData)
    {
        GstElement* rtcbin = static_cast<GstElement*>(userData);
        return GstWebRTCPeer::onOfferCreated(rtcbin, promise);
    };

    GstPromise* promise =
        gst_promise_new_with_change_func(
            onOfferCreatedCallback,
            rtcbin, nullptr);
    g_signal_emit_by_name(
        rtcbin, "create-offer", nullptr, promise);
}

// will be called from streaming thread
void GstWebRTCPeer::onAnswerCreated(
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
        postEos(rtcbin, true);
        return;
    }

    g_signal_emit_by_name(rtcbin,
        "set-local-description", sessionDescription, NULL);

    GCharPtr sdpPtr(gst_sdp_message_as_text(sessionDescription->sdp));
    postSdp(rtcbin, sdpPtr.get());
}

// will be called from streaming thread
void GstWebRTCPeer::onIceGatheringStateChanged(GstElement* rtcbin)
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
            postIceCandidate(rtcbin, 0, nullptr);
        }
    }
}

// will be called from streaming thread
void GstWebRTCPeer::onSetRemoteDescription(
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
            GstElement* rtcbin = static_cast<GstElement*>(userData);
            return GstWebRTCPeer::onAnswerCreated(rtcbin, promise);
        };

        GstPromise* promise =
            gst_promise_new_with_change_func(
                onAnswerCreatedCallback,
                rtcbin, nullptr);
        g_signal_emit_by_name(
            rtcbin, "create-answer", nullptr, promise);

        break;
    }
    default:
        GstWebRTCPeer::postEos(rtcbin, true);
        break;
    }
}

void GstWebRTCPeer::setRemoteSdp(const std::string& sdp) noexcept
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
            Role::Viewer == _role  ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER,
            sdpMessagePtr.release()));
    GstWebRTCSessionDescription* sessionDescription =
        sessionDescriptionPtr.get();

    auto onSetRemoteDescriptionCallback =
        (void (*) (GstPromise*, gpointer))
        [] (GstPromise* promise, gpointer userData)
    {
        GstElement* rtcbin = static_cast<GstElement*>(userData);
        return GstWebRTCPeer::onSetRemoteDescription(rtcbin, promise);
    };

    GstPromise* promise =
        gst_promise_new_with_change_func(
            onSetRemoteDescriptionCallback,
            rtcbin, nullptr);

    g_signal_emit_by_name(rtcbin,
        "set-remote-description", sessionDescription, promise);
}

const std::string& GstWebRTCPeer::sdp() noexcept
{
    return _sdp;
}

void GstWebRTCPeer::addIceCandidate(
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

    std::string resolvedCandidate;
    if(!candidate.empty())
        ResolveIceCandidate(candidate, &resolvedCandidate);

    if(!resolvedCandidate.empty()) {
        g_signal_emit_by_name(
            rtcbin, "add-ice-candidate",
            mlineIndex, resolvedCandidate.data());
    } else {
        g_signal_emit_by_name(
            rtcbin, "add-ice-candidate",
            mlineIndex, candidate.data());
    }
}

void GstWebRTCPeer::prepare(
    const IceServers& iceServers,
    const PreparedCallback& prepared,
    const IceCandidateCallback& iceCandidate,
    const EosCallback& eos) noexcept
{
    assert(!pipeline());
    if(pipeline())
        return;

    _iceServers = iceServers;
    _preparedCallback = prepared;
    _iceCandidateCallback = iceCandidate;
    _eosCallback = eos;

    prepare();

    if(!pipeline())
        onEos(true);
}
