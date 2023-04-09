#pragma once

#include "GstStreamingSource.h"


class GstRecordStreamer : public GstStreamingSource
{
public:
    typedef std::function<void ()> RecorderConnectedCallback;
    typedef std::function<void ()> RecorderDisconnectedCallback;
    GstRecordStreamer(
        const RecorderConnectedCallback&,
        const RecorderDisconnectedCallback&);

    std::unique_ptr<WebRTCPeer> createRecordPeer() noexcept override;

protected:
    bool prepare() noexcept override;
    void recordPrepare() noexcept;
    void cleanup() noexcept override;

    void onPrerolled() noexcept override;
    void peerAttached() noexcept override;
    void lastPeerDetached() noexcept override;

private:
    GstElement* webRtcBin() const noexcept;

    void srcPadAdded(GstElement* decodebin, GstPad*);
    void noMorePads(GstElement* decodebin);

    void recordPeerDestroyed(MessageProxy*);

private:
    const RecorderConnectedCallback _recorderConnectedCallback;
    const RecorderDisconnectedCallback _recorderDisconnectedCallback;

    GstElementPtr _rtcbinPtr;

    MessageProxy* _recordPeerProxy = nullptr;
};
