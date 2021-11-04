#pragma once

#include "GstStreamingSource.h"


class GstReStreamer2 : public GstStreamingSource
{
public:
    GstReStreamer2(const std::string& sourceUrl);

protected:
    void prepare() noexcept override;
    void cleanup() noexcept override;

private:
    void srcPadAdded(GstElement* decodebin, GstPad*);
    void noMorePads(GstElement* decodebin);

private:
    const std::string _sourceUrl;

    GstCapsPtr _h264CapsPtr;
    GstCapsPtr _vp8CapsPtr;
};
