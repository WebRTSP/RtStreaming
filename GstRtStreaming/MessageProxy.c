#include "MessageProxy.h"

#include <gst/gstmessage.h>


struct _MessageProxy
{
    GObject parent_instance;
};

G_DEFINE_TYPE(MessageProxy, message_proxy, G_TYPE_OBJECT)

static void message_proxy_class_init(MessageProxyClass* klass)
{
    g_signal_new(
        "tee", G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
        0, NULL, NULL, NULL, G_TYPE_NONE,
        1, GST_TYPE_ELEMENT);
    g_signal_new(
        "message", G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
        0, NULL, NULL, NULL, G_TYPE_NONE,
        1, GST_TYPE_MESSAGE);
    g_signal_new(
        "eos", G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
        0, NULL, NULL, NULL, G_TYPE_NONE,
        1, G_TYPE_BOOLEAN);
}

static void message_proxy_init(MessageProxy* self)
{

}

MessageProxy* message_proxy_new()
{
    return g_object_new(MESSAGE_PROXY_TYPE, NULL);
}
