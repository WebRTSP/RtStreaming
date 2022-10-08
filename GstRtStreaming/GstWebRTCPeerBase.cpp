#include "GstWebRTCPeerBase.h"

#include <cassert>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>
#include <agent.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstPtr.h>
#include <CxxPtr/GstWebRtcPtr.h>

#include "Helpers.h"


const bool GstWebRTCPeerBase::MDNSResolveRequired = GstRtStreaming::IsMDNSResolveRequired();
const bool GstWebRTCPeerBase::EndOfCandidatesSupported = GstRtStreaming::IsEndOfCandidatesSupported();
const bool GstWebRTCPeerBase::AddTurnServerSupported = GstRtStreaming::IsAddTurnServerSupported();
const bool GstWebRTCPeerBase::IceGatheringStateBroken = GstRtStreaming::IsIceGatheringStateBroken();

void GstWebRTCPeerBase::attachClient(
    const PreparedCallback& prepared,
    const IceCandidateCallback& iceCandidate,
    const EosCallback& eos) noexcept
{
    assert(!_clientAttached);
    if(_clientAttached) {
        return;
    }

    _preparedCallback = prepared;
    _iceCandidateCallback = iceCandidate;
    _eosCallback = eos;

    _clientAttached = true;
}

bool GstWebRTCPeerBase::clientAttached() const noexcept
{
    return _clientAttached;
}

void GstWebRTCPeerBase::setIceServers(const IceServers& iceServers) noexcept
{
    _iceServers = iceServers;
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

void GstWebRTCPeerBase::setWebRtcBin(GstElement* rtcbin) noexcept
{
    setWebRtcBin(GstElementPtr(GST_ELEMENT(gst_object_ref(rtcbin))));
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

void GstWebRTCPeerBase::setWebRtcBin(GstElementPtr&& rtcbinPtr) noexcept
{
    assert(rtcbinPtr && !_rtcbinPtr);

    if(!rtcbinPtr || _rtcbinPtr)
        return;

    _rtcbinPtr = std::move(rtcbinPtr);

    setIceServers();

    GstElement* rtcbin = webRtcBin();

    auto onConnectionStateChangedCallback =
        (void (*) (GstElement*, GParamSpec* , gpointer))
        [] (GstElement* rtcbin, GParamSpec*, gpointer) {
            GstWebRTCPeerBase::onConnectionStateChanged(rtcbin);
        };
    g_signal_connect(rtcbin,
        "notify::connection-state",
        G_CALLBACK(onConnectionStateChangedCallback), nullptr);

    auto onSignalingStateChangedCallback =
        (void (*) (GstElement*, GParamSpec* , gpointer))
        [] (GstElement* rtcbin, GParamSpec*, gpointer) {
            GstWebRTCPeerBase::onSignalingStateChanged(rtcbin);
        };
    g_signal_connect(rtcbin,
        "notify::signaling-state",
        G_CALLBACK(onSignalingStateChangedCallback), nullptr);

    auto onIceConnectionStateChangedCallback =
        (void (*) (GstElement*, GParamSpec* , gpointer))
        [] (GstElement* rtcbin, GParamSpec*, gpointer) {
            GstWebRTCPeerBase::onIceConnectionStateChanged(rtcbin);
        };
    g_signal_connect(rtcbin,
        "notify::ice-connection-state",
        G_CALLBACK(onIceConnectionStateChangedCallback), nullptr);

    if(GstRtStreaming::IsIceAgentAvailable()) {
        GstObject* iceAgent = nullptr;
        g_object_get(rtcbin, "ice-agent", &iceAgent, NULL);
        if(iceAgent) {
            GstObjectPtr iceAgentPtr(iceAgent);

            NiceAgent* niceAgent = nullptr;
            g_object_get(iceAgent, "agent", &niceAgent, NULL);

            if(niceAgent) {
                GObjectPtr niceAgentPtr(G_OBJECT(niceAgent));

                auto onSelectedPairCallback =
                    (void (*)(NiceAgent*, guint, guint, NiceCandidate*, NiceCandidate*, gpointer))
                    [] (NiceAgent* agent,
                        guint streamId, guint componentId,
                        NiceCandidate* localCandidate,
                        NiceCandidate* remoteCandidate,
                        gpointer userData)
                    {
                        GstElement* rtcbin = static_cast<GstElement*>(userData);
                        postLog(
                            rtcbin, spdlog::level::debug,
                            fmt::format(
                                "[GstWebRTCPeerBase] Selected ICE Pair: Local \"{}\" - Remote \"{}\"",
                                NiceCandidateToString(*localCandidate),
                                NiceCandidateToString(*remoteCandidate)));
                    };

                g_signal_connect(niceAgent,
                    "new-selected-pair-full",
                    G_CALLBACK(onSelectedPairCallback), rtcbin);
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
        if(!candidate)
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
void GstWebRTCPeerBase::postLog(
    GstElement* element,
    spdlog::level::level_enum level,
    const std::string& logMessage)
{
    GstBusPtr busPtr(gst_element_get_bus(element));
    if(!busPtr)
        return;

    GValue messageValue = G_VALUE_INIT;
    g_value_init(&messageValue, G_TYPE_STRING);
    g_value_take_string(&messageValue, g_strdup(logMessage.c_str()));

    GstStructure* structure =
        gst_structure_new_empty("log");

    gst_structure_set(
        structure,
        "level", G_TYPE_INT, level,
        NULL);

    gst_structure_take_value(
        structure,
        "message",
        &messageValue);

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(element), structure);

    gst_bus_post(busPtr.get(), message);
}

void GstWebRTCPeerBase::setIceServers()
{
    GstElement* rtcbin = webRtcBin();

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
                if(AddTurnServerSupported) {
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
void GstWebRTCPeerBase::onConnectionStateChanged(GstElement* rtcbin)
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
        postLog(rtcbin, spdlog::level::debug, fmt::format("[GstWebRTCPeerBase] Connection State changed: \"{}\"", stateName));
}

// will be called from streaming thread
void GstWebRTCPeerBase::onSignalingStateChanged(GstElement* rtcbin)
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
        postLog(rtcbin, spdlog::level::debug, fmt::format("[GstWebRTCPeerBase] Signaling State changed: \"{}\"", stateName));
}

// will be called from streaming thread
void GstWebRTCPeerBase::onIceConnectionStateChanged(GstElement* rtcbin)
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
        postLog(rtcbin, spdlog::level::debug, fmt::format("[GstWebRTCPeerBase] Ice Connection State changed: \"{}\"", stateName));
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
                rtcbin, "add-ice-candidate",
                mlineIndex, 0);
        }

        return;
    }

    if(MDNSResolveRequired) {
        std::string resolvedCandidate;
        if(!candidate.empty())
            GstRtStreaming::TryResolveMDNSIceCandidate(candidate, &resolvedCandidate);

        if(!resolvedCandidate.empty()) {
            g_signal_emit_by_name(
                rtcbin, "add-ice-candidate",
                mlineIndex, resolvedCandidate.c_str());
            return;
        }
    }

    g_signal_emit_by_name(
        rtcbin, "add-ice-candidate",
        mlineIndex, candidate.data());
}
