#pragma once

#include <string>
#include <deque>

#include <gst/gst.h>

#include "Types.h"


namespace GstRtcStreaming
{

IceServerType ParseIceServerType(const std::string& iceServer);

}
