#pragma once

#include "GstWebRTCPeer.h"


class GstReStreamer : public GstWebRTCPeer
{
public:
    GstReStreamer(
        const std::string& sourceUrl,
        const std::string& forceH264ProfileLevelId);
    ~GstReStreamer();

protected:
    void prepare(const WebRTCConfigPtr&) noexcept override;

private:
    void srcPadAdded(GstElement* decodebin, GstPad*);
    void noMorePads(GstElement* decodebin);

private:
    const std::string _sourceUrl;
    const std::string _forceH264ProfileLevelId;

    PreparedCallback _preparedCallback;
    IceCandidateCallback _iceCandidateCallback;
    EosCallback _eosCallback;

    WebRTCConfigPtr _webRTCConfig;

    GstCapsPtr _h264CapsPtr;
    GstCapsPtr _vp8CapsPtr;
};
