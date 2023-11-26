#pragma once

#include <gst/gst.h>

#include <spdlog/spdlog.h>


struct GstPipelineOwner
{
    // thread safe
    static void PostLog(GstElement*, spdlog::level::level_enum, const std::string& message);
};
