#pragma once

#include <functional>
#include <unordered_set>

#include "CxxPtr/GstPtr.h"

#include "../WebRTCPeer.h"

#include "MessageProxy.h"


class GstStreamingSource
{
public:
    virtual ~GstStreamingSource();

    std::unique_ptr<WebRTCPeer> createPeer() noexcept;

protected:
    GstStreamingSource();

    void setPipeline(GstElementPtr&&) noexcept;
    GstElement* pipeline() const noexcept;

    void setTee(GstElementPtr&&) noexcept;
    GstElement* tee() const noexcept;

    virtual void prepare() noexcept = 0;

    virtual void cleanup() noexcept;

private:
    gboolean onBusMessage(GstMessage*);

    static void postEos(
        GstElement* rtcbin,
        gboolean error);
    static void postTeePadRemoved(
        GstElement* tee);

    void onEos(bool error);
    void onTeePadRemoved();

    void peerDestroyed(MessageProxy*);

private:
    GstElementPtr _pipelinePtr;
    GstElementPtr _teePtr;
    GstElementPtr _fakeSinkPtr;

    std::deque<MessageProxyPtr> _waitingPeers;
    std::unordered_set<MessageProxy*> _peers;
};
