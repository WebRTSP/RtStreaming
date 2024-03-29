#pragma once

#include <functional>
#include <unordered_set>

#include "CxxPtr/GstPtr.h"

#include "../WebRTCPeer.h"

#include "GstPipelineOwner.h"
#include "Log.h"
#include "MessageProxy.h"


class GstStreamingSource: public GstPipelineOwner
{
public:
    virtual ~GstStreamingSource();

    std::unique_ptr<WebRTCPeer> createPeer() noexcept;
    virtual std::unique_ptr<WebRTCPeer> createRecordPeer() noexcept { return nullptr; }

protected:
    GstStreamingSource();

    void onEos(bool error);

    void setState(GstState state) noexcept;
    void pause() noexcept;
    void play() noexcept;
    void stop() noexcept;

    void setPipeline(GstElementPtr&&) noexcept;
    GstElement* pipeline() const noexcept;

    void setTee(GstElement*) noexcept;
    GstElement* tee() const noexcept;

    virtual bool prepare() noexcept = 0;
    GstElement* releasePipeline() noexcept;
    virtual void cleanup() noexcept;

    virtual void onPrerolled() noexcept {}
    virtual void onPeerAttached() noexcept;
    virtual void onLastPeerDetached() noexcept;
    virtual void onLastPeerDestroyed() noexcept {}

    unsigned peerCount() const noexcept;
    bool hasPeers() const noexcept;
    void destroyPeers() noexcept;

private:
    const std::shared_ptr<spdlog::logger>& log()
        { return _log; }

    gboolean onBusMessage(GstMessage*);

    static void postTeeAvailable(GstElement* tee);
    static void postTeePadAdded(GstElement* tee);
    static void postTeePadRemoved(GstElement* tee);

    void onTeeAvailable(GstElement* tee);
    void onTeePadAdded();
    void onTeePadRemoved();

    void onPeerDestroyed(MessageProxy*);
    void destroyPeer(MessageProxy*);

private:
    const std::shared_ptr<spdlog::logger> _log = GstRtStreamingLog();

    GstElementPtr _pipelinePtr;
    GstElementPtr _teePtr;
    GstElementPtr _fakeSinkPtr;

    bool _prerolled = false;

    std::deque<MessageProxyPtr> _waitingPeers;
    std::unordered_set<MessageProxy*> _peers;
};
