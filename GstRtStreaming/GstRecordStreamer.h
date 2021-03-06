#pragma once

#include "GstStreamingSource.h"


class GstRecordStreamer : public GstStreamingSource
{
public:
    GstRecordStreamer();

    std::unique_ptr<WebRTCPeer> createRecordPeer() noexcept override;

protected:
    void prepare() noexcept override;
    void recordPrepare() noexcept;
    void cleanup() noexcept override;

    void lastPeerDetached() noexcept override;

private:
    GstElement* webRtcBin() const noexcept;

    void srcPadAdded(GstElement* decodebin, GstPad*);
    void noMorePads(GstElement* decodebin);

    void recordPeerDestroyed(MessageProxy*);

private:
    GstElementPtr _rtcbinPtr;

    MessageProxy* _recordPeerProxy = nullptr;
};
