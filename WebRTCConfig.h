#pragma once

#include <optional>
#include <vector>
#include <memory>


struct WebRTCConfig
{
    typedef std::vector<std::string> IceServers;
    IceServers iceServers;
};

typedef std::shared_ptr<const WebRTCConfig> WebRTCConfigPtr;
