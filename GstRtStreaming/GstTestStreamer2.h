#pragma once

#include "Types.h"
#include "GstStreamingSource.h"


class GstTestStreamer2 : public GstStreamingSource
{
public:
    GstTestStreamer2(
        const std::string& pattern = std::string(),
        GstRtStreaming::Videocodec videocodec = GstRtStreaming::Videocodec::h264);

protected:
    void prepare() noexcept override;

private:
    const std::string _pattern;
    GstRtStreaming::Videocodec _videocodec;
};
