#pragma once

#include <memory>
#include <functional>

#include "RtcStreaming/WebRTCPeer.h"

#include "GstWebRTCPeer.h"


class GstClient : public GstWebRTCPeer
{
public:
    GstClient() : GstWebRTCPeer(Role::Viewer) {}

protected:
    void prepare() noexcept override;
};
