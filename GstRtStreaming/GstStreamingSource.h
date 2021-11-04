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

    void setState(GstState state) noexcept;
    void pause() noexcept;
    void play() noexcept;
    void stop() noexcept;

    void setPipeline(GstElementPtr&&) noexcept;
    GstElement* pipeline() const noexcept;

    void setTee(GstElementPtr&&) noexcept;
    GstElement* tee() const noexcept;

    virtual void prepare() noexcept = 0;
    virtual void cleanup() noexcept;

    virtual void peerAttached() noexcept;
    virtual void lastPeerDetached() noexcept;

    unsigned peerCount() const noexcept;
    bool hasPeers() const noexcept;

private:
    gboolean onBusMessage(GstMessage*);

    static void postTeePadAdded(GstElement* tee);
    static void postTeePadRemoved(GstElement* tee);

    void onEos(bool error);

    void onTeePadAdded();
    void onTeePadRemoved();

    void peerDestroyed(MessageProxy*);

private:
    GstElementPtr _pipelinePtr;
    GstElementPtr _teePtr;
    GstElementPtr _fakeSinkPtr;

    std::deque<MessageProxyPtr> _waitingPeers;
    std::unordered_set<MessageProxy*> _peers;
};
