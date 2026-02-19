#include "GstWebRTCPeerBase.h"

#include <cassert>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>
#include <nice/agent.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstPtr.h>
#include <CxxPtr/GstWebRtcPtr.h>

#include "Helpers.h"
#include "LoggerWrapper.h"


const bool GstWebRTCPeerBase::MDNSResolveRequired = GstRtStreaming::IsMDNSResolveRequired();
const bool GstWebRTCPeerBase::EndOfCandidatesSupported = GstRtStreaming::IsEndOfCandidatesSupported();
const bool GstWebRTCPeerBase::AddTurnServerSupported = GstRtStreaming::IsAddTurnServerSupported();
const bool GstWebRTCPeerBase::IceGatheringStateBroken = GstRtStreaming::IsIceGatheringStateBroken();
const bool GstWebRTCPeerBase::IsMinMaxRtpPortAvailable = GstRtStreaming::IsMinMaxRtpPortAvailable();
const bool GstWebRTCPeerBase::IsIceAgentAvailable = GstRtStreaming::IsIceAgentAvailable();

void GstWebRTCPeerBase::attachClient(
    const PreparedCallback& prepared,
    const IceCandidateCallback& iceCandidate,
    const EosCallback& eos,
    const std::string& logContext) noexcept
{
    assert(!_clientAttached);
    if(_clientAttached) {
        return;
    }

    _log = MakeGstRtStreamingMtLogger(_log->name(), logContext);

    _preparedCallback = prepared;
    _iceCandidateCallback = iceCandidate;
    _eosCallback = eos;

    _clientAttached = true;
}

bool GstWebRTCPeerBase::clientAttached() const noexcept
{
    return _clientAttached;
}

void GstWebRTCPeerBase::setPipeline(GstElement* pipeline) noexcept
{
    setPipeline(GstElementPtr(GST_ELEMENT(gst_object_ref(pipeline))));
}

void GstWebRTCPeerBase::setPipeline(GstElementPtr&& pipelinePtr) noexcept
{
    assert(pipelinePtr && !_pipelinePtr);

    if(!pipelinePtr || _pipelinePtr)
        return;

    _pipelinePtr = std::move(pipelinePtr);
}

GstElement* GstWebRTCPeerBase::pipeline() const noexcept
{
    return _pipelinePtr.get();
}

void GstWebRTCPeerBase::setWebRtcBin(
    const WebRTCConfig& webRTCConfig,
    GstElement* rtcbin) noexcept
{
    setWebRtcBin(webRTCConfig, GstElementPtr(GST_ELEMENT(gst_object_ref(rtcbin))));
}

namespace {

std::string NiceCandidateToString(const NiceCandidate& candidate)
{
    std::string out;
    out.reserve(NICE_ADDRESS_STRING_LEN + 1 + 5 + 1 + 11);

    switch(candidate.type) {
    case NICE_CANDIDATE_TYPE_HOST:
        out += "host";
        break;
    case NICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
        out += "srflx";
        break;
    case NICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
        out += "prflx";
        break;
    case NICE_CANDIDATE_TYPE_RELAYED:
        out += "relay";
        break;
    }

    out += " ";

    gchar adress[NICE_ADDRESS_STRING_LEN];
    nice_address_to_string(&candidate.addr, adress);
    out += adress;
    out += " ";

    switch(candidate.transport) {
    case NICE_CANDIDATE_TRANSPORT_UDP:
        out += "UDP";
        break;
    case NICE_CANDIDATE_TRANSPORT_TCP_ACTIVE:
        out += "TCP active";
        break;
    case NICE_CANDIDATE_TRANSPORT_TCP_PASSIVE:
        out += "TCP passive";
        break;
    case NICE_CANDIDATE_TRANSPORT_TCP_SO:
        out += "TCP so";
        break;
    }

    return out;
}

}

void GstWebRTCPeerBase::setWebRtcBin(
    const WebRTCConfig& webRTCConfig,
    GstElementPtr&& rtcbinPtr) noexcept
{
    assert(rtcbinPtr && !_rtcbinPtr);

    if(!rtcbinPtr || _rtcbinPtr)
        return;

    _rtcbinPtr = std::move(rtcbinPtr);

    setIceServers(webRTCConfig);

    GstElement* rtcbin = webRtcBin();

    g_object_set(rtcbin, "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_COMPAT, nullptr);

    auto onConnectionStateChangedCallback =
        + [] (GstElement* rtcbin, GParamSpec*, gpointer userData) {
            GstWebRTCPeerBase::onConnectionStateChanged(
                rtcbin,
                static_cast<LoggerWrapper*>(userData)->logger);
        };
    g_signal_connect_data(
        rtcbin,
        "notify::connection-state",
        G_CALLBACK(onConnectionStateChangedCallback),
        new LoggerWrapper { log() },
        LoggerWrapper::Destroy,
        G_CONNECT_DEFAULT);

    auto onSignalingStateChangedCallback =
        + [] (GstElement* rtcbin, GParamSpec*, gpointer userData) {
            GstWebRTCPeerBase::onSignalingStateChanged(
                rtcbin,
                static_cast<LoggerWrapper*>(userData)->logger);
        };
    g_signal_connect_data(
        rtcbin,
        "notify::signaling-state",
        G_CALLBACK(onSignalingStateChangedCallback),
        new LoggerWrapper { log() },
        LoggerWrapper::Destroy,
        G_CONNECT_DEFAULT);

    auto onIceConnectionStateChangedCallback =
        + [] (GstElement* rtcbin, GParamSpec*, gpointer userData) {
            GstWebRTCPeerBase::onIceConnectionStateChanged(
                rtcbin,
                static_cast<LoggerWrapper*>(userData)->logger);
        };
    g_signal_connect_data(
        rtcbin,
        "notify::ice-connection-state",
        G_CALLBACK(onIceConnectionStateChangedCallback),
        new LoggerWrapper { log() },
        LoggerWrapper::Destroy,
        G_CONNECT_DEFAULT);

    if(IsIceAgentAvailable) {
        GstObject* iceAgent = nullptr;
        g_object_get(rtcbin, "ice-agent", &iceAgent, NULL);
        if(iceAgent) {
            GstObjectPtr iceAgentPtr(iceAgent);

            if(IsMinMaxRtpPortAvailable) {
                if(webRTCConfig.minRtpPort)
                    g_object_set(iceAgent, "min-rtp-port", webRTCConfig.minRtpPort.value(), nullptr);
                if(webRTCConfig.maxRtpPort)
                    g_object_set(iceAgent, "max-rtp-port", webRTCConfig.maxRtpPort.value(), nullptr);
            }

            NiceAgent* niceAgent = nullptr;
            g_object_get(iceAgent, "agent", &niceAgent, NULL);

            if(niceAgent) {
                GObjectPtr niceAgentPtr(G_OBJECT(niceAgent));

                struct CallbackData {
                    GstElement* rtcbin;
                    const std::shared_ptr<spdlog::logger> logger;
                };

                auto onSelectedPairCallback =
                    + [] (
                        NiceAgent*,
                        guint streamId, guint componentId,
                        NiceCandidate* localCandidate,
                        NiceCandidate* remoteCandidate,
                        gpointer userData)
                    {
                        CallbackData* data = static_cast<CallbackData*>(userData);
                        data->logger->debug(
                            "Selected ICE Pair ({}/{}): Local \"{}\" - Remote \"{}\"",
                            streamId, componentId,
                            NiceCandidateToString(*localCandidate),
                            NiceCandidateToString(*remoteCandidate));
                    };

                g_signal_connect_data(
                    niceAgent,
                    "new-selected-pair-full",
                    G_CALLBACK(onSelectedPairCallback),
                    new CallbackData { rtcbin, log() },
                    [] (gpointer userData, GClosure*) { delete static_cast<CallbackData*>(userData); },
                    G_CONNECT_DEFAULT);
            }
        }
    }
}

GstElement* GstWebRTCPeerBase::webRtcBin() const noexcept
{
    return _rtcbinPtr.get();
}

void GstWebRTCPeerBase::onIceCandidate(
    unsigned mlineIndex,
    const gchar* candidate)
{
    if(_iceCandidateCallback) {
        if(!candidate || candidate[0] == '\0')
            _iceCandidateCallback(mlineIndex, "a=end-of-candidates");
        else
            _iceCandidateCallback(mlineIndex, std::string("a=") + candidate);
    }
}

void GstWebRTCPeerBase::onSdp(const gchar* sdp)
{
    _sdp = sdp;

    onPrepared();
}

void GstWebRTCPeerBase::onPrepared()
{
    if(_preparedCallback)
        _preparedCallback();
}

void GstWebRTCPeerBase::onEos(bool /*error*/)
{
    if(_eosCallback)
        _eosCallback();
}

// will be called from streaming thread
void GstWebRTCPeerBase::setIceServers(const WebRTCConfig& webRTCConfig)
{
    GstElement* rtcbin = webRtcBin();

    for(const std::string& iceServer: webRTCConfig.iceServers) {
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
                if(AddTurnServerSupported) {
                    gboolean ret;
                    g_signal_emit_by_name(
                        rtcbin,
                        "add-turn-server",
                        iceServer.c_str(),
                        &ret);
                } else {
                    g_object_set(
                        rtcbin,
                        "turn-server",
                        iceServer.c_str(),
                        nullptr);
                }
                break;
            }
            default:
                break;
        }
    }

    if(webRTCConfig.useRelayTransport)
        gst_util_set_object_arg(G_OBJECT(rtcbin), "ice-transport-policy", "relay");
}

// will be called from streaming thread
void GstWebRTCPeerBase::onConnectionStateChanged(
    GstElement* rtcbin,
    const std::shared_ptr<spdlog::logger>& log)
{
    GstWebRTCPeerConnectionState state = GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED;
    g_object_get(rtcbin, "connection-state", &state, NULL);

    const gchar* stateName = "Unknown";
    switch(state) {
    case GST_WEBRTC_PEER_CONNECTION_STATE_NEW:
        stateName = "GST_WEBRTC_PEER_CONNECTION_STATE_NEW";
        break;
    case GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING:
        stateName = "GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING";
        break;
    case GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED:
        stateName = "GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED";
        break;
    case GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED:
        stateName = "GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED";
        break;
    case GST_WEBRTC_PEER_CONNECTION_STATE_FAILED:
        stateName = "GST_WEBRTC_PEER_CONNECTION_STATE_FAILED";
        break;
    case GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED:
        stateName = "GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED";
        break;
    }

    if(stateName)
        log->debug("Connection State changed: \"{}\"", stateName);
}

// will be called from streaming thread
void GstWebRTCPeerBase::onSignalingStateChanged(
    GstElement* rtcbin,
    const std::shared_ptr<spdlog::logger>& log)
{
    GstWebRTCSignalingState state = GST_WEBRTC_SIGNALING_STATE_CLOSED;
    g_object_get(rtcbin, "signaling-state", &state, NULL);

    const gchar* stateName = "Unknown";
    switch(state) {
    case GST_WEBRTC_SIGNALING_STATE_STABLE:
        stateName = "GST_WEBRTC_SIGNALING_STATE_STABLE";
        break;
    case GST_WEBRTC_SIGNALING_STATE_CLOSED:
        stateName = "GST_WEBRTC_SIGNALING_STATE_CLOSED";
        break;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_OFFER:
        stateName = "GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_OFFER";
        break;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER:
        stateName = "GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_OFFER";
        break;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_PRANSWER:
        stateName = "GST_WEBRTC_SIGNALING_STATE_HAVE_LOCAL_PRANSWER";
        break;
    case GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_PRANSWER:
        stateName = "GST_WEBRTC_SIGNALING_STATE_HAVE_REMOTE_PRANSWER";
        break;
    }

    if(stateName)
        log->debug("Signaling State changed: \"{}\"", stateName);
}

// will be called from streaming thread
void GstWebRTCPeerBase::onIceConnectionStateChanged(
    GstElement* rtcbin,
    const std::shared_ptr<spdlog::logger>& log)
{
    GstWebRTCICEConnectionState state = GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED;
    g_object_get(rtcbin, "ice-connection-state", &state, NULL);

    const gchar* stateName = "Unknown";
    switch(state) {
    case GST_WEBRTC_ICE_CONNECTION_STATE_NEW:
        stateName = "GST_WEBRTC_ICE_CONNECTION_STATE_NEW";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING:
        stateName = "GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED:
        stateName = "GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED:
        stateName = "GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_FAILED:
        stateName = "GST_WEBRTC_ICE_CONNECTION_STATE_FAILED";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED:
        stateName = "GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED";
        break;
    case GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED:
        stateName = "GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED";
        break;
    }

    if(stateName)
        log->debug("Ice Connection State changed: \"{}\"", stateName);
}

const std::string& GstWebRTCPeerBase::sdp() noexcept
{
    return _sdp;
}

void GstWebRTCPeerBase::addIceCandidate(
    unsigned mlineIndex,
    const std::string& candidate) noexcept
{
    GstElement* rtcbin = webRtcBin();

    if(candidate.empty() || candidate == "a=end-of-candidates") {
        if(EndOfCandidatesSupported) {
            g_signal_emit_by_name(
                rtcbin,
                "add-ice-candidate",
                mlineIndex,
                0);
        }

        return;
    }

    if(MDNSResolveRequired) {
        std::string resolvedCandidate;
        if(!candidate.empty())
            GstRtStreaming::TryResolveMDNSIceCandidate(candidate, &resolvedCandidate);

        if(!resolvedCandidate.empty()) {
            g_signal_emit_by_name(
                rtcbin,
                "add-ice-candidate",
                mlineIndex,
                resolvedCandidate.c_str());
            return;
        }
    }

    g_signal_emit_by_name(
        rtcbin,
        "add-ice-candidate",
        mlineIndex,
        candidate.data());
}
