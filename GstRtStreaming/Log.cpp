#include "Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>


static std::shared_ptr<spdlog::logger> Logger;

void InitGstRtStreamingLogger(spdlog::level::level_enum level)
{
    spdlog::sink_ptr sink = std::make_shared<spdlog::sinks::stdout_sink_st>();

    Logger = std::make_shared<spdlog::logger>("GstRtStreaming", sink);

    Logger->set_level(level);
}

const std::shared_ptr<spdlog::logger>& GstRtStreamingLog()
{
    if(!Logger)
#ifndef NDEBUG
        InitGstRtStreamingLogger(spdlog::level::debug);
#else
        InitGstRtStreamingLogger(spdlog::level::info);
#endif

    return Logger;
}
