#pragma once

#include <string>
#include <deque>

#include <gst/gst.h>

#include "Types.h"


namespace GstRtStreaming
{

IceServerType ParseIceServerType(const std::string& iceServer);

}
