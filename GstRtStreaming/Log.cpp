#include "Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>


namespace {

static std::shared_ptr<spdlog::logger> Logger;

void ConfigureLogger(const std::shared_ptr<spdlog::logger>& logger, const std::string& context)
{
    logger->set_level(GstRtStreamingLog()->level());

    if(!context.empty()) {
#ifdef SNAPCRAFT_BUILD
        logger->set_pattern("[" + context + "] [%n] [%l] %v");
#else
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [" + context + "] [%n] [%l] %v");
#endif
    } else {
#ifdef SNAPCRAFT_BUILD
        logger->set_pattern("[%n] [%l] %v");
#endif
    }
}

}

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

std::shared_ptr<spdlog::logger> MakeGstRtStreamingMtLogger(
    const std::string& name,
    const std::string& context)
{
    // have to go long road to avoid issues with duplicated names in loggers registry
    std::shared_ptr<spdlog::logger> mtLogger = std::make_shared<spdlog::logger>(
        !name.empty() ? name : GstRtStreamingLog()->name(),
        std::make_shared<spdlog::sinks::stdout_sink_mt>());

    ConfigureLogger(mtLogger, context);

    return mtLogger;
}
