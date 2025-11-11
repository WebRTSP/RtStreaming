#pragma once

#include <optional>
#include <filesystem>

#include "GstStreamingSource.h"


class GstRecordStreamer : public GstStreamingSource
{
public:
    struct RecordOptions {
        std::filesystem::path dir;
        guint64 maxFileSize = 100 * 1024 * 1024; // 100Mb
    };

    typedef std::function<void ()> RecorderConnectedCallback;
    typedef std::function<void ()> RecorderDisconnectedCallback;
    GstRecordStreamer(
        const std::optional<RecordOptions>& recordOptions = {},
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
    const std::shared_ptr<spdlog::logger>& log() const
        { return _log; }

    GstElement* webRtcBin() const noexcept;

    void srcPadAdded(GstElement* decodebin, GstPad*);
    void noMorePads(GstElement* decodebin);

    void onRecordPeerDestroyed(MessageProxy*);

    bool isRecordToStorageEnabled() const { return _recordOptions.has_value(); }
    void finalizeRecording(GstElement* pipeline);

private:
    const std::shared_ptr<spdlog::logger> _log = GstRtStreamingLog();

    const std::optional<RecordOptions> _recordOptions;
    const RecorderConnectedCallback _recorderConnectedCallback;
    const RecorderDisconnectedCallback _recorderDisconnectedCallback;

    gulong _padAddedHandlerId = 0;
    gulong _noMorePadsHandlerId = 0;

    GstElementPtr _rtcbinPtr;

    MessageProxy* _recordPeerProxy = nullptr;
};
