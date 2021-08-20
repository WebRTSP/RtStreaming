#include "GstClient.h"

#include <cassert>
#include <deque>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/webrtc_fwd.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstPtr.h>

#include "LibGst.h"


void GstClient::prepare() noexcept
{
    GstElementPtr pipelinePtr(gst_pipeline_new("Client Pipeline"));
    GstElement* pipeline = pipelinePtr.get();

    GstElementPtr rtcbinPtr(gst_element_factory_make("webrtcbin", "clientrtcbin"));
    GstElement* rtcbin = rtcbinPtr.get();

    gst_bin_add_many(GST_BIN(pipeline), rtcbin, NULL);
    gst_object_ref(rtcbin);

    GstCapsPtr capsPtr(gst_caps_from_string("application/x-rtp, media=video"));
    GstWebRTCRTPTransceiver* recvonlyTransceiver = nullptr;
    g_signal_emit_by_name(
        rtcbin, "add-transceiver",
        GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY, capsPtr.get(),
        &recvonlyTransceiver);
    GstWebRTCRTPTransceiverPtr recvonlyTransceiverPtr(recvonlyTransceiver);

    auto onPadAddedCallback =
        (void (*) (GstElement* webrtc, GstPad* pad, gpointer* userData))
    [] (GstElement* webrtc, GstPad* pad, gpointer* userData)
    {
        GstPad *sink;

        GstElement* pipeline = reinterpret_cast<GstElement*>(userData);

        if(GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
            return;

        GstElement* out =
            gst_parse_bin_from_description(
#if 1
                "rtph264depay ! avdec_h264 ! "
#else
                "rtpvp8depay ! vp8dec ! "
#endif
                "videoconvert ! queue ! "
                "autovideosink", TRUE, NULL);
        gst_bin_add(GST_BIN(pipeline), out);
        gst_element_sync_state_with_parent(out);

        sink = (GstPad*)out->sinkpads->data;

        gst_pad_link(pad, sink);
    };
    g_signal_connect(rtcbin, "pad-added",
        G_CALLBACK(onPadAddedCallback), pipeline);

    setPipeline(std::move(pipelinePtr));
    setWebRtcBin(std::move(rtcbinPtr));

    pause();
}
