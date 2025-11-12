#pragma once

#include <functional>
#include <deque>
#include <optional>

#include "CxxPtr/GstPtr.h"

#include "../WebRTCPeer.h"

#include "Log.h"


class GstWebRTCPeerBase : public WebRTCPeer
{
public:
    void addIceCandidate(unsigned mlineIndex, const std::string& candidate) noexcept override;
    const std::string& sdp() noexcept override;

protected:
    static const bool MDNSResolveRequired;
    static const bool EndOfCandidatesSupported;
    static const bool AddTurnServerSupported;
    static const bool IceGatheringStateBroken;
    static const bool IsMinMaxRtpPortAvailable;
    static const bool IsIceAgentAvailable;

    const std::shared_ptr<spdlog::logger>& log() const
        { return _log; }

    void attachClient(
        const PreparedCallback&,
        const IceCandidateCallback&,
        const EosCallback&,
        const std::string& logContext) noexcept;
    bool clientAttached() const noexcept;

    void setPipeline(GstElement*) noexcept;
    virtual void setPipeline(GstElementPtr&&) noexcept;
    GstElement* pipeline() const noexcept;

    void setWebRtcBin(const WebRTCConfig&, GstElement*) noexcept;
    virtual void setWebRtcBin(const WebRTCConfig&, GstElementPtr&&) noexcept;
    GstElement* webRtcBin() const noexcept;

    static void onConnectionStateChanged(
        GstElement* rtcbin,
        const std::shared_ptr<spdlog::logger>&);
    static void onSignalingStateChanged(
        GstElement* rtcbin,
        const std::shared_ptr<spdlog::logger>&);
    static void onIceConnectionStateChanged(
        GstElement* rtcbin,
        const std::shared_ptr<spdlog::logger>&);

    void onIceCandidate(
        unsigned mlineIndex,
        const gchar* candidate);
    void onSdp(const gchar* sdp);
    void onPrepared();
    void onEos(bool /*error*/);

private:
    void setIceServers(const WebRTCConfig&);

private:
    std::shared_ptr<spdlog::logger> _log = MakeGstRtStreamingMtLogger("GstWebRTCPeer");

    bool _clientAttached = false;

    PreparedCallback _preparedCallback;
    IceCandidateCallback _iceCandidateCallback;
    EosCallback _eosCallback;

    GstElementPtr _pipelinePtr;
    GstElementPtr _rtcbinPtr;

    std::string _sdp;
};
