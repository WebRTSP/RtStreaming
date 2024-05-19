#include "GstV4L2Streamer.h"

#include <cassert>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstPtr.h>

#include "LibGst.h"
#include "Helpers.h"
#include "Log.h"


static const auto Log = GstRtStreamingLog;

GstV4L2Streamer::GstV4L2Streamer(
    const std::optional<std::string>& edidFilePath,
    const std::optional<VideoResolution>& resolution,
    const std::optional<std::string>& h264Level,
    bool useHwEncoder) :
    _edidFilePath(edidFilePath),
    _resolution(resolution),
    _h264Level(h264Level),
    _useHwEncoder(useHwEncoder)
{
}

bool GstV4L2Streamer::setEdid()
{
    if(!_edidFilePath) return true;

    Log()->info("Setting EDID with \"v4l2-ctl\" from \"{}\"...", *_edidFilePath);

    const std::string filePrefix = "--set-edid=file=";
    std::string edidArg = filePrefix + *_edidFilePath;

    const gchar* argv[] = { "v4l2-ctl", edidArg.c_str(), "--fix-edid-checksums", nullptr };
    gint waitStatus;
    GError* error = nullptr;
    GSpawnFlags flags = GSpawnFlags(
        G_SPAWN_SEARCH_PATH | G_SPAWN_FILE_AND_ARGV_ZERO |
        G_SPAWN_CHILD_INHERITS_STDOUT | G_SPAWN_CHILD_INHERITS_STDERR |
        G_SPAWN_STDIN_FROM_DEV_NULL);
    if(!g_spawn_sync(
        nullptr,
        const_cast<gchar**>(argv),
        nullptr,
        flags,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &waitStatus,
        &error) ||
        !g_spawn_check_wait_status(waitStatus, &error))
    {
        Log()->error("Failed to launch v4l2-ctl: {}", error->message);
        g_error_free(error);
        return false;
    }

    Log()->info("EDID successfully set");

    return true;
}

bool GstV4L2Streamer::prepare() noexcept
{
    if(!setEdid()) return false;

    const char* pipelineDesc;
    if(_useHwEncoder) {
        pipelineDesc =
            "v4l2src ! "
            "capsfilter name=sourceFilter ! "
            "v4l2h264enc ! "
            "capsfilter name=encoderFilter ! "
            "rtph264pay pt=99 config-interval=-1 ! tee name = tee";
    } else {
        pipelineDesc =
            "v4l2src ! "
            "capsfilter name=sourceFilter ! "
            "x264enc ! "
            "capsfilter name=encoderFilter ! "
            "rtph264pay pt=99 config-interval=-1 ! tee name = tee";
    }

    GError* parseError = nullptr;
    GstElementPtr pipelinePtr(gst_parse_launch(pipelineDesc, &parseError));
    GErrorPtr parseErrorPtr(parseError);
    if(parseError) {
        Log()->error("Failed to parse pipeline: {}", parseError->message);
        return false;
    }

    GstElement* pipeline = pipelinePtr.get();
    GstElementPtr teePtr(gst_bin_get_by_name(GST_BIN(pipeline), "tee"));

    GstElementPtr sourceFilterPtr(gst_bin_get_by_name(GST_BIN(pipeline), "sourceFilter"));
    std::string sourceCaps =
        "video/x-raw,"
        "framerate=30/1,format=UYVY,interlace-mode=(string)progressive";
    if(_resolution) {
        sourceCaps += ",";
        sourceCaps += "width=" + std::to_string(_resolution->width) + ",";
        sourceCaps += "height=" + std::to_string(_resolution->height);
    }
    gst_util_set_object_arg(G_OBJECT(sourceFilterPtr.get()), "caps", sourceCaps.c_str());

    GstElementPtr encoderFilterPtr(gst_bin_get_by_name(GST_BIN(pipeline), "encoderFilter"));
    std::string encoderCaps =
        "video/x-h264,"
        "profile=(string)constrained-baseline,";
    if(_h264Level) {
        encoderCaps += "level=(string)" + _h264Level.value();
    } else {
        encoderCaps += "level=(string)4";
    }
    gst_util_set_object_arg(G_OBJECT(encoderFilterPtr.get()), "caps", encoderCaps.c_str());

    setPipeline(std::move(pipelinePtr));
    setTee(teePtr.get());

    return true;
}
