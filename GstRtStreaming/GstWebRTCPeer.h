#pragma once

#include <functional>

#include <gst/webrtc/webrtc_fwd.h>

#include "CxxPtr/GstPtr.h"

#include "GstWebRTCPeerBase.h"


class GstWebRTCPeer : public GstWebRTCPeerBase
{
public:
    void prepare(
        const WebRTCConfigPtr&,
        const PreparedCallback&,
        const IceCandidateCallback&,
        const EosCallback&,
        const std::string& logContext) noexcept override;

    void setRemoteSdp(const std::string& sdp) noexcept override;

protected:
    enum class Role {
        Viewer,
        Streamer
    };

    GstWebRTCPeer(Role) noexcept;
    ~GstWebRTCPeer() noexcept;

    void setPipeline(GstElementPtr&&) noexcept override;
    void setWebRtcBin(const WebRTCConfig&, GstElementPtr&&) noexcept override;

    void setState(GstState) noexcept;
    void pause() noexcept;
    void play() noexcept final override;
    void stop() noexcept final override;

    virtual void prepare(const WebRTCConfigPtr&) noexcept = 0;

    virtual gboolean onBusMessage(GstMessage*) noexcept;

private:
    static void onNegotiationNeeded(
        GstElement* rtcbin) noexcept;
    static void onIceGatheringStateChanged(
        GstElement* rtcbin) noexcept;

    static void postIceCandidate(
        GstElement* rtcbin,
        guint mlineIndex,
        const gchar* candidate) noexcept;
    static void postSdp(
        GstElement* rtcbin,
        const gchar* sdp);
    static void postEos(
        GstElement* rtcbin,
        gboolean error) noexcept;

    static void onOfferCreated(
        GstElement* rtcbin,
        GstPromise*) noexcept;
    static void onAnswerCreated(
        GstElement* rtcbin,
        GstPromise*) noexcept;
    static void onSetRemoteDescription(
        GstElement* rtcbin,
        GstPromise*) noexcept;

private:
    const Role _role;
};
