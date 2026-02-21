#include "GstReStreamer2.h"

#include <cassert>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstPtr.h>

#include "Helpers.h"


GstReStreamer2::GstReStreamer2(
    const std::string& forceH264ProfileLevelId) :
    _forceH264ProfileLevelId(forceH264ProfileLevelId)
{
}

GstReStreamer2::GstReStreamer2(
    const std::string& sourceUrl,
    const std::string& forceH264ProfileLevelId) :
    _sourceUrl(sourceUrl),
    _forceH264ProfileLevelId(forceH264ProfileLevelId)
{
}

void GstReStreamer2::setSourceUrl(const std::string& sourceUrl)
{
    _sourceUrl = sourceUrl;
}

bool GstReStreamer2::prepare() noexcept
{
    if(pipeline())
        return true; // already prepared

    _h264CapsPtr.reset(gst_caps_from_string("video/x-h264"));
#if USE_H265
    _h265CapsPtr.reset(gst_caps_from_string("video/x-h265"));
#endif
    _vp8CapsPtr.reset(gst_caps_from_string("video/x-vp8"));

    GstCapsPtr supportedCapsPtr(gst_caps_copy(_h264CapsPtr.get()));
#if USE_H265
    gst_caps_append(supportedCapsPtr.get(), gst_caps_copy(_h265CapsPtr.get()));
#endif
    gst_caps_append(supportedCapsPtr.get(), gst_caps_copy(_vp8CapsPtr.get()));
    GstCaps* supportedCaps = supportedCapsPtr.get();

    setPipeline(GstElementPtr(gst_pipeline_new(nullptr)));
    GstElement* pipeline = this->pipeline();

    GstElementPtr srcPtr(gst_element_factory_make("uridecodebin", nullptr));
    GstElement* decodebin = srcPtr.get();
    if(!decodebin)
        return false;

    g_object_set(decodebin, "caps", supportedCaps, nullptr);

    auto srcPadAddedCallback =
        + [] (GstElement* decodebin, GstPad* pad, gpointer userData) {
            GstReStreamer2* self = static_cast<GstReStreamer2*>(userData);
            self->srcPadAdded(decodebin, pad);
        };
    g_signal_connect(decodebin, "pad-added", G_CALLBACK(srcPadAddedCallback), this);

    auto noMorePadsCallback =
        + [] (GstElement* decodebin, gpointer userData) {
            GstReStreamer2* self = static_cast<GstReStreamer2*>(userData);
            self->noMorePads(decodebin);
        };
    g_signal_connect(decodebin, "no-more-pads", G_CALLBACK(noMorePadsCallback), this);

    g_object_set(decodebin,
        "uri", _sourceUrl.c_str(),
        nullptr);

    gst_bin_add(GST_BIN(pipeline), srcPtr.release());

    play();

    return true;
}

void GstReStreamer2::srcPadAdded(
    GstElement* /*decodebin*/,
    GstPad* pad)
{
    GstElement* pipeline = this->pipeline();

    GstCapsPtr capsPtr(gst_pad_get_current_caps(pad));
    GstCaps* caps = capsPtr.get();

    GstElement* payBin = nullptr;
    if(gst_caps_is_always_compatible(caps, _h264CapsPtr.get())) {
        const bool forceH264ProfileLevelId = !_forceH264ProfileLevelId.empty();

        std::string repayPipelineDesc =
            "h264parse config-interval=-1 ! rtph264pay pt=96";
        if(forceH264ProfileLevelId)
            repayPipelineDesc += " ! capssetter name=capssetter";

        payBin =
            gst_parse_bin_from_description(
                repayPipelineDesc.c_str(),
                TRUE, NULL);
        gst_bin_add(GST_BIN(pipeline), payBin);
        gst_element_sync_state_with_parent(payBin);

        GstPad* sink = (GstPad*)payBin->sinkpads->data;

        if(GST_PAD_LINK_OK != gst_pad_link(pad, sink))
            assert(false);

        if(forceH264ProfileLevelId) {
            GstElementPtr capsSetterPtr(gst_bin_get_by_name(GST_BIN(payBin), "capssetter"));
            const std::string caps = "application/x-rtp,profile-level-id=(string)" + _forceH264ProfileLevelId;
            gst_util_set_object_arg(G_OBJECT(capsSetterPtr.get()), "caps", caps.c_str());
        }
#if USE_H265
    } else if(gst_caps_is_always_compatible(caps, _h265CapsPtr.get())) {
        std::string repayPipelineDesc =
            "h265parse config-interval=-1 ! rtph265pay pt=97";
        payBin = gst_parse_bin_from_description(
            repayPipelineDesc.c_str(),
            TRUE, NULL);
        gst_bin_add(GST_BIN(pipeline), payBin);
        gst_element_sync_state_with_parent(payBin);

        GstPad* sink = (GstPad*)payBin->sinkpads->data;

        if(GST_PAD_LINK_OK != gst_pad_link(pad, sink))
            assert(false);
#endif
    } else if(gst_caps_is_always_compatible(caps, _vp8CapsPtr.get())) {
        payBin =
            gst_parse_bin_from_description(
                "rtpvp8pay pt=96",
                TRUE, NULL);
        gst_bin_add(GST_BIN(pipeline), payBin);
        gst_element_sync_state_with_parent(payBin);

        GstPad* sink = (GstPad*)payBin->sinkpads->data;

        if(GST_PAD_LINK_OK != gst_pad_link(pad, sink))
            assert(false);
    } else
        return;

    GstElementPtr teePtr(gst_element_factory_make("tee", nullptr));
    GstElement* tee = teePtr.get();
    gst_bin_add(GST_BIN(pipeline), GST_ELEMENT(gst_object_ref(tee)));
    gst_element_sync_state_with_parent(tee);
    gst_element_link(payBin, tee);

    setTee(tee);
}

void GstReStreamer2::noMorePads(GstElement* /*decodebin*/)
{
}

void GstReStreamer2::cleanup() noexcept
{
    _h264CapsPtr.reset();
#if USE_H265
    _h265CapsPtr.reset();
#endif
    _vp8CapsPtr.reset();

    GstStreamingSource::cleanup();
}
