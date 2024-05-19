#pragma once

#include "Types.h"
#include "GstStreamingSource.h"


class GstV4L2Streamer : public GstStreamingSource
{
public:
    struct VideoResolution {
        unsigned width;
        unsigned height;
    };

    GstV4L2Streamer(
        const std::optional<std::string>& edidFilePath = std::optional<std::string>(),
        const std::optional<VideoResolution>& resolution = std::optional<VideoResolution>(),
        const std::optional<std::string>& h264Level = std::optional<const std::string>(),
        bool useHwEncoder = true);

protected:
    bool prepare() noexcept override;

private:
    bool setEdid();

private:
    const std::optional<std::string> _edidFilePath;
    const std::optional<VideoResolution> _resolution;
    const std::optional<std::string> _h264Level;
    const bool _useHwEncoder;
};
