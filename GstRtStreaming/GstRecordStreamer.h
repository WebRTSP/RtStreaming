#pragma once

#include "GstStreamingSource.h"


class GstRecordStreamer : public GstStreamingSource
{
public:
    typedef std::function<void ()> RecorderConnectedCallback;
    typedef std::function<void ()> RecorderDisconnectedCallback;
    GstRecordStreamer(
        const RecorderConnectedCallback& = RecorderConnectedCallback(),
        const RecorderDisconnectedCallback& = RecorderDisconnectedCallback());

    std::unique_ptr<WebRTCPeer> createRecordPeer() noexcept override;

protected:
    bool prepare() noexcept override;
    void recordPrepare() noexcept;
    void cleanup() noexcept override;

    void onPrerolled() noexcept override;
    void onPeerAttached() noexcept override;
    void onLastPeerDetached() noexcept override;

private:
    GstElement* webRtcBin() const noexcept;

    void srcPadAdded(GstElement* decodebin, GstPad*);
    void noMorePads(GstElement* decodebin);

    void onRecordPeerDestroyed(MessageProxy*);

private:
    const RecorderConnectedCallback _recorderConnectedCallback;
    const RecorderDisconnectedCallback _recorderDisconnectedCallback;

    GstElementPtr _rtcbinPtr;

    MessageProxy* _recordPeerProxy = nullptr;
};
