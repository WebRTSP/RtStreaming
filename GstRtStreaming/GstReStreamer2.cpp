#include "GstReStreamer2.h"

#include <cassert>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/rtptransceiver.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstPtr.h>

#include "Helpers.h"


GstReStreamer2::GstReStreamer2(const std::string& sourceUrl) :
    _sourceUrl(sourceUrl)
{
}

void GstReStreamer2::setState(GstState state) noexcept
{
    if(!pipeline()) {
        if(state != GST_STATE_NULL)
            ;
        return;
    }

    switch(gst_element_set_state(pipeline(), state)) {
        case GST_STATE_CHANGE_FAILURE:
            break;
        case GST_STATE_CHANGE_SUCCESS:
            break;
        case GST_STATE_CHANGE_ASYNC:
            break;
        case GST_STATE_CHANGE_NO_PREROLL:
            break;
    }
}

void GstReStreamer2::prepare() noexcept
{
    assert(!pipeline());
    if(pipeline())
        return;

    _h264CapsPtr.reset(gst_caps_from_string("video/x-h264"));
    _vp8CapsPtr.reset(gst_caps_from_string("video/x-vp8"));

    GstCapsPtr supportedCapsPtr(gst_caps_copy(_h264CapsPtr.get()));
    gst_caps_append(supportedCapsPtr.get(), gst_caps_copy(_vp8CapsPtr.get()));
    GstCaps* supportedCaps = supportedCapsPtr.get();

    setPipeline(GstElementPtr(gst_pipeline_new(nullptr)));
    GstElement* pipeline = this->pipeline();

    GstElementPtr srcPtr(gst_element_factory_make("uridecodebin", nullptr));
    GstElement* decodebin = srcPtr.get();
    if(!decodebin)
        return;

    g_object_set(decodebin, "caps", supportedCaps, nullptr);

    auto srcPadAddedCallback =
        (void (*)(GstElement*, GstPad*, gpointer))
         [] (GstElement* decodebin, GstPad* pad, gpointer userData)
    {
        GstReStreamer2* self = static_cast<GstReStreamer2*>(userData);
        self->srcPadAdded(decodebin, pad);
    };
    g_signal_connect(decodebin, "pad-added", G_CALLBACK(srcPadAddedCallback), this);

    auto noMorePadsCallback =
        (void (*)(GstElement*,  gpointer))
         [] (GstElement* decodebin, gpointer userData)
    {
        GstReStreamer2* self = static_cast<GstReStreamer2*>(userData);
        self->noMorePads(decodebin);
    };
    g_signal_connect(decodebin, "no-more-pads", G_CALLBACK(noMorePadsCallback), this);

    g_object_set(decodebin,
        "uri", _sourceUrl.c_str(),
        nullptr);

    gst_bin_add(GST_BIN(pipeline), srcPtr.release());

    setState(GST_STATE_PLAYING);
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
        payBin =
            gst_parse_bin_from_description(
                "h264parse config-interval=-1 ! rtph264pay pt=96 ! "
                "capssetter caps=\"application/x-rtp,profile-level-id=(string)42c015\"",
                TRUE, NULL);
        gst_bin_add(GST_BIN(pipeline), payBin);
        gst_element_sync_state_with_parent(payBin);

        GstPad* sink = (GstPad*)payBin->sinkpads->data;

        if(GST_PAD_LINK_OK != gst_pad_link(pad, sink))
            assert(false);
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

    setTee(std::move(teePtr));
}

void GstReStreamer2::noMorePads(GstElement* /*decodebin*/)
{
}

void GstReStreamer2::cleanup() noexcept
{
    _h264CapsPtr.reset();
    _vp8CapsPtr.reset();

    GstStreamingSource::cleanup();
}
