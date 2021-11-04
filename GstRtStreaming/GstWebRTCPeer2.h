#pragma once

#include <functional>

#include "CxxPtr/GstPtr.h"

#include "../WebRTCPeer.h"

#include "MessageProxy.h"


class GstWebRTCPeer2 : public WebRTCPeer
{
public:
    GstWebRTCPeer2(MessageProxy*, GstElement* pipeline);
    ~GstWebRTCPeer2();

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
    GstElement* tee() const noexcept;
    GstPad* teeSrcPad() const noexcept;
    GstElement* queue() const noexcept;
    GstElement* webRtcBin() const noexcept;

    void setIceServers();

    void onMessage(GstMessage*);

private:
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

    void onIceCandidate(
        unsigned mlineIndex,
        const gchar* candidate);
    void onSdp(const gchar* sdp);
    void onPrepared();
    void onEos(bool /*error*/);

private:
    MessageProxy* _messageProxy;
    gulong _teeHandlerId = 0;
    gulong _messageHandlerId = 0;
    gulong _eosHandlerId = 0;

    std::deque<std::string> _iceServers;
    PreparedCallback _preparedCallback;
    IceCandidateCallback _iceCandidateCallback;
    EosCallback _eosCallback;

    GstElementPtr _pipelinePtr;
    GstElementPtr _teePtr;
    GstPadPtr _teeSrcPadPtr;
    GstElementPtr _queuePtr;
    GstElementPtr _rtcbinPtr;

    std::string _sdp;
};
