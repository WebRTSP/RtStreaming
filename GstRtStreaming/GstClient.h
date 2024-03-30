#pragma once

#include <memory>
#include <functional>

#include "../WebRTCPeer.h"

#include "GstWebRTCPeer.h"


class GstClient : public GstWebRTCPeer
{
public:
    GstClient() : GstWebRTCPeer(Role::Viewer) {}

protected:
    void prepare(const WebRTCConfigPtr&) noexcept override;
};
