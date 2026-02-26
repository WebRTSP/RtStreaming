#pragma once

#include "GstStreamingSource.h"


class GstReStreamer2 : public GstStreamingSource
{
public:
    GstReStreamer2(
        const std::string& sourceUrl,
        const std::string& forceH264ProfileLevelId) noexcept;

protected:
    void setSourceUrl(const std::string&) noexcept;
    bool prepare() noexcept override;

private:
    void srcPadAdded(GstElement* decodebin, GstPad*) noexcept;
    void noMorePads(GstElement* decodebin) noexcept;

private:
    std::string _sourceUrl;
    const std::string _forceH264ProfileLevelId;

    GstCapsPtr _h264CapsPtr;
#if USE_H265
    GstCapsPtr _h265CapsPtr;
#endif
    GstCapsPtr _vp8CapsPtr;
};
