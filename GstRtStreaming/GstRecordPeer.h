#pragma once

#include <functional>

#include "CxxPtr/GstPtr.h"

#include "GstWebRTCPeerBase.h"

#include "MessageProxy.h"


class GstRecordPeer : public GstWebRTCPeerBase
{
public:
    GstRecordPeer(
        MessageProxy*,
        GstElement* pipeline,
        GstElement* rtcbin);
    ~GstRecordPeer();

    void prepare(
        const WebRTCConfigPtr&,
        const PreparedCallback&,
        const IceCandidateCallback&,
        const EosCallback&) noexcept override;

    void setRemoteSdp(const std::string& sdp) noexcept override;

protected:
    void onMessage(GstMessage*);

private:
    void setState(GstState) noexcept;
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

    static void onAnswerCreated(
        MessageProxy*,
        GstElement* rtcbin,
        GstPromise*);
    static void onSetRemoteDescription(
        MessageProxy*,
        GstElement* rtcbin,
        GstPromise*);

    void internalPrepare() noexcept;

private:
    MessageProxy* _messageProxy;
    gulong _messageHandlerId = 0;
    gulong _eosHandlerId = 0;

    gulong _iceGatheringStateHandlerId = 0;
    gulong _iceCandidateHandlerId = 0;

    GstElementPtr _rtcbinPtr;
};
