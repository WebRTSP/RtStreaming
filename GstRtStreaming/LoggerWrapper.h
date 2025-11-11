#pragma once

#include <glib.h>
#include <glib-object.h>

#include <spdlog/spdlog.h>


struct LoggerWrapper
{
    const std::shared_ptr<spdlog::logger> logger;

    static void Destroy(gpointer userData, GClosure*)
        { delete static_cast<LoggerWrapper*>(userData); }
};
