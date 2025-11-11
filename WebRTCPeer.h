#pragma once

#include <string>
#include <functional>
#include <deque>
#include <memory>

#include "WebRTCConfig.h"


struct WebRTCPeer
{
    virtual ~WebRTCPeer() {}

    typedef std::function<void ()> PreparedCallback;
    typedef std::function<
        void (unsigned mlineIndex, const std::string& candidate)> IceCandidateCallback;
    typedef std::function<void ()> EosCallback;
    virtual void prepare(
        const WebRTCConfigPtr&, // FIXME? is it too expensive to use std::shared_ptr here?
        const PreparedCallback&,
        const IceCandidateCallback&,
        const EosCallback&,
        const std::string& logContext) noexcept = 0;

    virtual const std::string& sdp() noexcept = 0;

    virtual void setRemoteSdp(const std::string& sdp) noexcept = 0;
    virtual void addIceCandidate(
        unsigned mlineIndex,
        const std::string& candidate) noexcept = 0;

    virtual void play() noexcept = 0;
    virtual void stop() noexcept = 0;
};
