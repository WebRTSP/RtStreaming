#include "Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>


static std::shared_ptr<spdlog::logger> Logger;


void InitGstRtStreamingLogger(spdlog::level::level_enum level)
{
    if(!Logger) {
        Logger = spdlog::stdout_logger_st("GstRtStreaming");
#ifdef SNAPCRAFT_BUILD
        Logger->set_pattern("[%n] [%l] %v");
#endif
    }

    Logger->set_level(level);
}

const std::shared_ptr<spdlog::logger>& GstRtStreamingLog()
{
    if(!Logger) {
#ifdef NDEBUG
        InitGstRtStreamingLogger(spdlog::level::info);
#else
        InitGstRtStreamingLogger(spdlog::level::debug);
#endif
    }

    return Logger;
}
