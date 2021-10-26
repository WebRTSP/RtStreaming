#include "GstPipelineStreamer2.h"

#include <cassert>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>
#include <gst/webrtc/webrtc.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstPtr.h>

#include "LibGst.h"
#include "Helpers.h"


GstPipelineStreamer2::GstPipelineStreamer2(const std::string& sourcePipelineDesc) :
    _sourcePipelineDesc(sourcePipelineDesc)
{
}

void GstPipelineStreamer2::prepare() noexcept
{
    GstElementPtr pipelinePtr(gst_pipeline_new(nullptr));
    GstElement* pipeline = pipelinePtr.get();

    GError* parseError = nullptr;
    GstElement* sourceBin =
        gst_parse_bin_from_description(
            _sourcePipelineDesc.c_str(),
            TRUE, &parseError);
    if(parseError)
        return;

    gst_bin_add(GST_BIN(pipeline), sourceBin);
    gst_element_sync_state_with_parent(sourceBin);

    GstElementPtr teePtr(gst_element_factory_make("tee", nullptr));
    GstElement* tee = teePtr.get();
    if(!tee)
        return;

    gst_bin_add(GST_BIN(pipeline), GST_ELEMENT(gst_object_ref(tee)));
    gst_element_sync_state_with_parent(tee);
    if(!gst_element_link(sourceBin, tee))
        return;

    setPipeline(std::move(pipelinePtr));
    setTee(std::move(teePtr));
}
