#pragma once

#include <optional>
#include <vector>
#include <memory>


struct WebRTCConfig
{
    typedef std::vector<std::string> IceServers;
    IceServers iceServers;

    std::optional<uint16_t> minRtpPort;
    std::optional<uint16_t> maxRtpPort;

    bool useRelayTransport = false;
};

typedef std::shared_ptr<const WebRTCConfig> WebRTCConfigPtr;
