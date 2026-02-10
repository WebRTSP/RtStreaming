#include "LibGst.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

#include <gst/gst.h>


#ifdef __ANDROID__
void LogToLogcat(
    GstDebugCategory* category,
    GstDebugLevel level,
    const gchar* file,
    const gchar* function,
    gint line,
    GObject* object,
    GstDebugMessage* message,
    gpointer user_data)
{
    android_LogPriority logPriority;
    switch(level) {
        case GST_LEVEL_NONE:
            logPriority = ANDROID_LOG_UNKNOWN;
            break;
        case GST_LEVEL_ERROR:
            logPriority = ANDROID_LOG_ERROR;
            break;
        case GST_LEVEL_WARNING:
            logPriority = ANDROID_LOG_WARN;
            break;
        case GST_LEVEL_FIXME:
        case GST_LEVEL_INFO:
            logPriority = ANDROID_LOG_INFO;
            break;
        case GST_LEVEL_DEBUG:
            logPriority = ANDROID_LOG_DEBUG;
            break;
        case GST_LEVEL_LOG:
        case GST_LEVEL_TRACE:
        case GST_LEVEL_MEMDUMP:
            logPriority = ANDROID_LOG_VERBOSE;
            break;
        default:
            logPriority = ANDROID_LOG_UNKNOWN;
            break;
    }

    __android_log_print(
        logPriority,
        gst_debug_category_get_name(category),
        "%s\n",
        gst_debug_message_get(message));
}

G_BEGIN_DECLS
    GST_PLUGIN_STATIC_DECLARE(debug);
    GST_PLUGIN_STATIC_DECLARE(coreelements);
    GST_PLUGIN_STATIC_DECLARE(videoconvertscale);
    GST_PLUGIN_STATIC_DECLARE(nice);
    GST_PLUGIN_STATIC_DECLARE(rtp);
    GST_PLUGIN_STATIC_DECLARE(srtp);
    GST_PLUGIN_STATIC_DECLARE(dtls);
    GST_PLUGIN_STATIC_DECLARE(rtpmanager);
    GST_PLUGIN_STATIC_DECLARE(libav);
    GST_PLUGIN_STATIC_DECLARE(webrtc);
G_END_DECLS
#endif

LibGst::LibGst()
{
#ifdef __ANDROID__
    gst_debug_remove_log_function(gst_debug_log_default);
    gst_debug_add_log_function(LogToLogcat, nullptr, nullptr);
    gst_debug_set_default_threshold(GST_LEVEL_WARNING);
    gst_debug_set_active(TRUE);
#endif

    gst_init(0, 0);

#ifdef __ANDROID__
    GST_PLUGIN_STATIC_REGISTER(debug);
    GST_PLUGIN_STATIC_REGISTER(coreelements);
    GST_PLUGIN_STATIC_REGISTER(videoconvertscale);
    GST_PLUGIN_STATIC_REGISTER(nice);
    GST_PLUGIN_STATIC_REGISTER(rtp);
    GST_PLUGIN_STATIC_REGISTER(srtp);
    GST_PLUGIN_STATIC_REGISTER(dtls);
    GST_PLUGIN_STATIC_REGISTER(rtpmanager);
    GST_PLUGIN_STATIC_REGISTER(libav);
    GST_PLUGIN_STATIC_REGISTER(webrtc);
#endif
}
