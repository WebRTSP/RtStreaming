#pragma once

#include "Types.h"
#include "GstWebRTCPeer.h"


class GstTestStreamer : public GstWebRTCPeer
{
public:
    GstTestStreamer(
        const std::string& pattern = std::string(),
        GstRtcStreaming::Videocodec videocodec = GstRtcStreaming::Videocodec::h264);

protected:
    void prepare() noexcept override;

private:
    const std::string _pattern;
    GstRtcStreaming::Videocodec _videocodec;
};
