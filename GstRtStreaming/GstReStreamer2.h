#pragma once

#include "GstStreamingSource.h"


class GstReStreamer2 : public GstStreamingSource
{
public:
    GstReStreamer2(
        const std::string& sourceUrl,
        const std::string& forceH264ProfileLevelId);

protected:
    GstReStreamer2(const std::string& forceH264ProfileLevelId);

    void setSourceUrl(const std::string&);
    bool prepare() noexcept override;
    void cleanup() noexcept override;

private:
    void srcPadAdded(GstElement* decodebin, GstPad*);
    void noMorePads(GstElement* decodebin);

private:
    std::string _sourceUrl;
    const std::string _forceH264ProfileLevelId;

    GstCapsPtr _h264CapsPtr;
    GstCapsPtr _vp8CapsPtr;
};
