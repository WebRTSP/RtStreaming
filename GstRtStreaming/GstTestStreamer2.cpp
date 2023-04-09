#include "GstTestStreamer2.h"

#include <cassert>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstPtr.h>

#include "LibGst.h"
#include "Helpers.h"


GstTestStreamer2::GstTestStreamer2(
    const std::string& pattern,
    GstRtStreaming::Videocodec videocodec) :
    _pattern(pattern), _videocodec(videocodec)
{
}

bool GstTestStreamer2::prepare() noexcept
{
    std::string usePattern = "smpte";
    if(_pattern == "bars")
        usePattern = "smpte100";
    else if(
        _pattern == "white" ||
        _pattern == "red" ||
        _pattern == "green" ||
        _pattern == "blue")
    {
        usePattern = _pattern;
    }

    const char* pipelineDesc;
    if(_videocodec == GstRtStreaming::Videocodec::h264) {
        pipelineDesc =
            "videotestsrc name=src ! "
            "x264enc ! video/x-h264, profile=baseline ! rtph264pay pt=96 ! "
            "tee name=tee";
    } else {
        pipelineDesc =
            "videotestsrc name=src ! "
            "vp8enc ! rtpvp8pay pt=96 ! "
            "tee name=tee";
    }

    GError* parseError = nullptr;
    GstElementPtr pipelinePtr(gst_parse_launch(pipelineDesc, &parseError));
    GErrorPtr parseErrorPtr(parseError);
    if(parseError)
        return false;

    GstElement* pipeline = pipelinePtr.get();

    GstElementPtr srcPtr(gst_bin_get_by_name(GST_BIN(pipeline), "src"));
    GstElement* src = srcPtr.get();

    gst_util_set_object_arg(G_OBJECT(src), "pattern", usePattern.c_str());

    GstElementPtr teePtr(
        gst_bin_get_by_name(GST_BIN(pipeline), "tee"));

    setPipeline(std::move(pipelinePtr));
    setTee(teePtr.get());

    return true;
}
