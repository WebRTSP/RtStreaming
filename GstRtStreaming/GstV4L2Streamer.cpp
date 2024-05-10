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
    std::optional<VideoResolution> resolution,
    std::optional<std::string> h264Level,
    bool useHwEncoder) :
    _resolution(resolution),
    _h264Level(h264Level),
    _useHwEncoder(useHwEncoder)
{
}

bool GstV4L2Streamer::prepare() noexcept
{
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
