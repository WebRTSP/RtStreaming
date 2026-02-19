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
            rtcbin,
            "add-transceiver",
            GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,
            capsPtr.get(),
            &recvonlyTransceiver);
        GstWebRTCRTPTransceiverPtr recvonlyTransceiverPtr(recvonlyTransceiver);
    }
    {
        GstCapsPtr capsPtr(gst_caps_from_string("application/x-rtp, media=audio"));
        GstWebRTCRTPTransceiver* recvonlyTransceiver = nullptr;
        g_signal_emit_by_name(
            rtcbin,
            "add-transceiver",
            GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY,
            capsPtr.get(),
            &recvonlyTransceiver);
        GstWebRTCRTPTransceiverPtr recvonlyTransceiverPtr(recvonlyTransceiver);
    }

    auto onPadAddedCallback =
        + [] (GstElement* webrtc, GstPad* pad, gpointer* userData) {
            GstClient* self = reinterpret_cast<GstClient*>(userData);
            GstElement* pipeline = self->pipeline();

            if(GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
                return;

            GstCapsPtr padCapsPtr(gst_pad_get_current_caps(pad));
            GstCapsPtr h264CapsPtr(gst_caps_from_string("application/x-rtp, media=video, encoding-name=H264"));
            GstCapsPtr vp8CapsPtr(gst_caps_from_string("application/x-rtp, media=video, encoding-name=VP8"));
            GstCapsPtr opusCapsPtr(gst_caps_from_string("application/x-rtp, media=audio, encoding-name=OPUS"));
            GstCaps* padCaps = padCapsPtr.get();
            GstCaps* h264Caps = h264CapsPtr.get();
            GstCaps* vp8Caps = vp8CapsPtr.get();
            GstCaps* opusCaps = opusCapsPtr.get();

            const gchar* decodeBinDescription = nullptr;
            bool video = true;
            if(gst_caps_is_always_compatible(padCaps, h264Caps)) {
                decodeBinDescription = "rtph264depay ! avdec_h264 ! videoconvert ! queue";
            } else if(gst_caps_is_always_compatible(padCaps, vp8Caps)) {
                decodeBinDescription = "rtpvp8depay ! vp8dec ! videoconvert ! queue";
            } else if(gst_caps_is_always_compatible(padCaps, opusCaps)) {
                decodeBinDescription = "rtpopusdepay ! opusdec ! audioconvert ! queue";
                video = false;
            }

            if(decodeBinDescription) {
                GstElement* decodeBin = gst_parse_bin_from_description(
                    decodeBinDescription,
                    TRUE,
                    nullptr);

                gst_bin_add(GST_BIN(pipeline), decodeBin);
                gst_element_sync_state_with_parent(decodeBin);
                GstPad* sinkPad = (GstPad*)decodeBin->sinkpads->data;
                gst_pad_link(pad, sinkPad);

                GstElement* sink;
                if(video) {
                    sink = self->_showVideoStats ?
                        gst_element_factory_make("fpsdisplaysink", nullptr) :
                        gst_element_factory_make("autovideosink", nullptr);

                    g_object_set(sink, "sync", self->_sync ? TRUE : FALSE, nullptr);
                } else {
                    sink =  gst_element_factory_make("autoaudiosink", nullptr);
                }
                gst_bin_add(GST_BIN(pipeline), sink);
                gst_element_sync_state_with_parent(sink);
                gst_element_link(decodeBin, sink);
            }
        };
    g_signal_connect(
        rtcbin,
        "pad-added",
        G_CALLBACK(onPadAddedCallback),
        this);

    setPipeline(std::move(pipelinePtr));
    setWebRtcBin(*webRTCConfig, std::move(rtcbinPtr));

    pause();
}
