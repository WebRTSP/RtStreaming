#pragma once

#include <functional>

#include "CxxPtr/GstPtr.h"

#include "GstWebRTCPeerBase.h"

#include "MessageProxy.h"


class GstWebRTCPeer2 : public GstWebRTCPeerBase
{
public:
    GstWebRTCPeer2(MessageProxy*);
    ~GstWebRTCPeer2();

    void prepare(
        const WebRTCConfigPtr&,
        const PreparedCallback&,
        const IceCandidateCallback&,
        const EosCallback&) noexcept override;

    void setRemoteSdp(const std::string& sdp) noexcept override;

protected:
    GstElement* tee() const noexcept;
    GstElement* queue() const noexcept;

    void onMessage(GstMessage*);

private:
    const std::shared_ptr<spdlog::logger>& log()
        { return _log; }

    void play() noexcept {}
    void stop() noexcept {}

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

    static void onOfferCreated(
        MessageProxy*,
        GstElement* rtcbin,
        GstPromise*);
    static void onAnswerCreated(
        MessageProxy*,
        GstElement* rtcbin,
        GstPromise*);
    static void onSetRemoteDescription(
        MessageProxy*,
        GstElement* rtcbin,
        GstPromise*);

    void internalPrepare() noexcept;
    void prepareWebRtcBin() noexcept;

private:
    const std::shared_ptr<spdlog::logger> _log = GstRtStreamingLog();

    MessageProxy* _messageProxy;
    gulong _teeHandlerId = 0;
    gulong _messageHandlerId = 0;
    gulong _eosHandlerId = 0;

    WebRTCConfigPtr _webRTCConfig;

    GstElementPtr _teePtr;
    GstElementPtr _queuePtr;

    std::atomic_flag _prepared = ATOMIC_FLAG_INIT;
    gulong _prepareProbe = 0;
};
