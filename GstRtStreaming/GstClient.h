#pragma once

#include <memory>
#include <functional>

#include "../WebRTCPeer.h"

#include "GstWebRTCPeer.h"


class GstClient : public GstWebRTCPeer
{
public:
    GstClient(bool showVideoStats = false, bool sync = true):
        GstWebRTCPeer(Role::Viewer), _showVideoStats(showVideoStats), _sync(sync) {}

protected:
    void prepare(const WebRTCConfigPtr&) noexcept override;

private:
    const bool _showVideoStats;
    const bool _sync;
};
