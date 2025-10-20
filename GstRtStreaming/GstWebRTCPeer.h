#pragma once

#include <functional>

#include "CxxPtr/GstPtr.h"

#include "GstWebRTCPeerBase.h"


class GstWebRTCPeer : public GstWebRTCPeerBase
{
public:
    void prepare(
        const WebRTCConfigPtr&,
        const PreparedCallback&,
        const IceCandidateCallback&,
        const EosCallback&) noexcept override;

    void setRemoteSdp(const std::string& sdp) noexcept override;

protected:
    enum class Role {
        Viewer,
        Streamer
    };

    GstWebRTCPeer(Role);
    ~GstWebRTCPeer();

    void setPipeline(GstElementPtr&&) noexcept override;
    void setWebRtcBin(const WebRTCConfig&, GstElementPtr&&) noexcept override;

    void setState(GstState) noexcept;
    void pause() noexcept;
    void play() noexcept override;
    void stop() noexcept override;

    virtual void prepare(const WebRTCConfigPtr&) noexcept = 0;

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

private:
    const Role _role;
};
