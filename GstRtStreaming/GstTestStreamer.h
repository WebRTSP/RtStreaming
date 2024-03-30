#pragma once

#include "Types.h"
#include "GstWebRTCPeer.h"


class GstTestStreamer : public GstWebRTCPeer
{
public:
    GstTestStreamer(
        const std::string& pattern = std::string(),
        GstRtStreaming::Videocodec videocodec = GstRtStreaming::Videocodec::h264);

protected:
    void prepare(const WebRTCConfigPtr&) noexcept override;

private:
    const std::string _pattern;
    GstRtStreaming::Videocodec _videocodec;
};
