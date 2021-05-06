#pragma once

#include <functional>

#include "CxxPtr/GstPtr.h"

#include "../WebRTCPeer.h"


class GstWebRTCPeer : public WebRTCPeer
{
public:
    void prepare(
        const IceServers&,
        const PreparedCallback&,
        const IceCandidateCallback&,
        const EosCallback&) noexcept override;

    void setRemoteSdp(const std::string& sdp) noexcept override;
    void addIceCandidate(unsigned mlineIndex, const std::string& candidate) noexcept override;
    const std::string& sdp() noexcept override;

protected:
    static void ResolveIceCandidate(
        const std::string& candidate,
        std::string* resolvedCandidate);

    enum class Role {
        Viewer,
        Streamer
    };

    GstWebRTCPeer(Role);
    ~GstWebRTCPeer();

    void setPipeline(GstElementPtr&&) noexcept;
    GstElement* pipeline() const noexcept;

    void setWebRtcBin(GstElementPtr&&) noexcept;
    GstElement* webRtcBin() const noexcept;

    void setIceServers();

    void setState(GstState) noexcept;
    void pause() noexcept;
    void play() noexcept;
    void stop() noexcept;

    virtual void prepare() noexcept = 0;

private:
    gboolean onBusMessage(GstMessage*);

    static void onNegotiationNeeded(
        GstElement* rtcbin);
    static void onIceGatheringStateChanged(
        GstElement* rtcbin);

    static void postIceCandidate(
        GstElement* rtcbin,
        guint mlineIndex,
        const gchar* candidate);
    static void postSdp(
        GstElement* rtcbin,
        const gchar* sdp);
    static void postEos(
        GstElement* rtcbin,
        gboolean error);

    static void onOfferCreated(
        GstElement* rtcbin,
        GstPromise*);
    static void onAnswerCreated(
        GstElement* rtcbin,
        GstPromise*);
    static void onSetRemoteDescription(
        GstElement* rtcbin,
        GstPromise*);

    void onIceCandidate(
        unsigned mlineIndex,
        const gchar* candidate);
    void onSdp(const gchar* sdp);
    void onPrepared();
    void onEos(bool error);

private:
    const Role _role;

    std::deque<std::string> _iceServers;
    PreparedCallback _preparedCallback;
    IceCandidateCallback _iceCandidateCallback;
    EosCallback _eosCallback;

    GstElementPtr _pipelinePtr;
    GstElementPtr _rtcbinPtr;

    std::string _sdp;
};
