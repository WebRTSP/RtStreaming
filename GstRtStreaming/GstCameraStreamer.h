#pragma once

#include "Types.h"
#include "GstStreamingSource.h"


class GstCameraStreamer : public GstStreamingSource
{
public:
    struct VideoResolution {
        unsigned width;
        unsigned height;
    };

    GstCameraStreamer(
        const std::optional<VideoResolution>& resolution = std::optional<VideoResolution>(),
        const std::optional<std::string>& h264Level = std::optional<std::string>(),
        bool useHwEncoder = true);

protected:
    bool prepare() noexcept override;

private:
    const std::optional<VideoResolution> _resolution;
    const std::optional<std::string> _h264Level;
    const bool _useHwEncoder;
};
