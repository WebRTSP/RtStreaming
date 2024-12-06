#include "GstClient.h"

#include <cassert>
#include <deque>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <gst/webrtc/webrtc_fwd.h>

#include <CxxPtr/GlibPtr.h>
#include <CxxPtr/GstPtr.h>
#include <CxxPtr/GstWebRtcPtr.h>

#include "LibGst.h"


void GstClient::prepare(const WebRTCConfigPtr& webRTCConfig) noexcept
{
    GstElementPtr pipelinePtr(gst_pipeline_new("Client Pipeline"));
    GstElement* pipeline = pipelinePtr.get();

    GstElementPtr rtcbinPtr(gst_element_factory_make("webrtcbin", "clientrtcbin"));
    GstElement* rtcbin = rtcbinPtr.get();

    gst_bin_add_many(GST_BIN(pipeline), GST_ELEMENT(gst_object_ref(rtcbin)), NULL);

    {
        GstCapsPtr capsPtr(gst_caps_from_string("application/x-rtp, media=video"));
        GstWebRTCRTPTransceiver* recvonlyTransceiver = nullptr;
        g_signal_emit_by_name(
            rtcbin, "add-transceiver",
            GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY, capsPtr.get(),
            &recvonlyTransceiver);
        GstWebRTCRTPTransceiverPtr recvonlyTransceiverPtr(recvonlyTransceiver);
    }
    {
        GstCapsPtr capsPtr(gst_caps_from_string("application/x-rtp, media=audio"));
        GstWebRTCRTPTransceiver* recvonlyTransceiver = nullptr;
        g_signal_emit_by_name(
            rtcbin, "add-transceiver",
            GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY, capsPtr.get(),
            &recvonlyTransceiver);
        GstWebRTCRTPTransceiverPtr recvonlyTransceiverPtr(recvonlyTransceiver);
    }

    auto onPadAddedCallback =
        (void (*) (GstElement* webrtc, GstPad* pad, gpointer* userData))
    [] (GstElement* webrtc, GstPad* pad, gpointer* userData)
    {
        GstElement* pipeline = reinterpret_cast<GstElement*>(userData);

        if(GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
            return;

        g_autoptr(GstCaps) padCaps = gst_pad_get_current_caps(pad);
        g_autoptr(GstCaps) h264Caps = gst_caps_from_string("application/x-rtp, media=video, encoding-name=H264");
        g_autoptr(GstCaps) vp8Caps = gst_caps_from_string("application/x-rtp, media=video, encoding-name=VP8");
        g_autoptr(GstCaps) opusCaps = gst_caps_from_string("application/x-rtp, media=audio, encoding-name=OPUS");

        if(gst_caps_is_always_compatible(padCaps, h264Caps)) {
            GstElement* out =
                gst_parse_bin_from_description(
                    "rtph264depay ! avdec_h264 ! "
                    "videoconvert ! queue ! "
                    "autovideosink", TRUE, NULL);
            gst_bin_add(GST_BIN(pipeline), out);
            gst_element_sync_state_with_parent(out);

            GstPad* sink = (GstPad*)out->sinkpads->data;

            gst_pad_link(pad, sink);
        } else if(gst_caps_is_always_compatible(padCaps, vp8Caps)) {
            GstElement* out =
                gst_parse_bin_from_description(
                    "rtpvp8depay ! vp8dec ! "
                    "videoconvert ! queue ! "
                    "autovideosink", TRUE, NULL);
            gst_bin_add(GST_BIN(pipeline), out);
            gst_element_sync_state_with_parent(out);

            GstPad* sink = (GstPad*)out->sinkpads->data;

            gst_pad_link(pad, sink);
        } else if(gst_caps_is_always_compatible(padCaps, opusCaps)) {
            GstElement* out =
                gst_parse_bin_from_description(
                    "rtpopusdepay ! opusdec ! "
                    "audioconvert ! queue ! "
                    "autoaudiosink", TRUE, NULL);
            gst_bin_add(GST_BIN(pipeline), out);
            gst_element_sync_state_with_parent(out);

            GstPad* sink = (GstPad*)out->sinkpads->data;

            gst_pad_link(pad, sink);
        }
    };
    g_signal_connect(rtcbin, "pad-added",
        G_CALLBACK(onPadAddedCallback), pipeline);

    setPipeline(std::move(pipelinePtr));
    setWebRtcBin(*webRTCConfig, std::move(rtcbinPtr));

    pause();
}
