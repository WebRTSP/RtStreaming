#pragma once

#include <string>
#include <deque>

#include <gst/gst.h>

#include "Types.h"


namespace GstRtStreaming
{

bool IsMDNSResolveRequired();
bool IsEndOfCandidatesSupported();
bool IsAddTurnServerSupported();
bool IsIceGatheringStateBroken();
bool IsIceAgentAvailable();

IceServerType ParseIceServerType(const std::string& iceServer);

void TryResolveMDNSIceCandidate(
    const std::string& candidate,
    std::string* resolvedCandidate);

}
