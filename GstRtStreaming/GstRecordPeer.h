#pragma once

#include <functional>

#include "CxxPtr/GstPtr.h"

#include "../WebRTCPeer.h"

#include "MessageProxy.h"


class GstRecordPeer : public WebRTCPeer
{
public:
    GstRecordPeer(
        MessageProxy*,
        GstElement* pipeline,
        GstElement* rtcbin);
    ~GstRecordPeer();

    void prepare(
        const IceServers&,
        const PreparedCallback&,
        const IceCandidateCallback&,
        const EosCallback&) noexcept override;

    void setRemoteSdp(const std::string& sdp) noexcept override;
    void addIceCandidate(unsigned mlineIndex, const std::string& candidate) noexcept override;
    const std::string& sdp() noexcept override;

protected:
    GstElement* pipeline() const noexcept;
    GstElement* webRtcBin() const noexcept;

    void setIceServers();

    void onMessage(GstMessage*);

private:
    void setState(GstState) noexcept;
    void play() noexcept;
    void stop() noexcept;

    static void onNegotiationNeeded(
        MessageProxy*,
        GstElement* rtcbin);
    static void onIceGatheringStateChanged(
        MessageProxy*,
        GstElement* rtcbin);

    static void postIceCandidate(
        MessageProxy*,
        GstElement* rtcbin,
        guint mlineIndex,
        const gchar* candidate);
    static void postSdp(
        MessageProxy*,
        GstElement* rtcbin,
        const gchar* sdp);
    static void postEos(
        MessageProxy*,
        GstElement* rtcbin,
        gboolean error);

    static void onAnswerCreated(
        MessageProxy*,
        GstElement* rtcbin,
        GstPromise*);
    static void onSetRemoteDescription(
        MessageProxy*,
        GstElement* rtcbin,
        GstPromise*);

    void internalPrepare() noexcept;

    void onIceCandidate(
        unsigned mlineIndex,
        const gchar* candidate);
    void onSdp(const gchar* sdp);
    void onPrepared();
    void onEos(bool /*error*/);

private:
    MessageProxy* _messageProxy;
    gulong _messageHandlerId = 0;
    gulong _eosHandlerId = 0;

    gulong _iceGatheringStateHandlerId = 0;
    gulong _iceCandidateHandlerId = 0;

    std::deque<std::string> _iceServers;
    PreparedCallback _preparedCallback;
    IceCandidateCallback _iceCandidateCallback;
    EosCallback _eosCallback;

    GstElementPtr _pipelinePtr;
    GstElementPtr _rtcbinPtr;

    std::string _sdp;
};
