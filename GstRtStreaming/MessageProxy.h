#pragma once

#include <glib-object.h>


G_BEGIN_DECLS

#define MESSAGE_PROXY_TYPE message_proxy_get_type()
G_DECLARE_FINAL_TYPE(MessageProxy, message_proxy, , MESSAGE_PROXY, GObject)

MessageProxy* message_proxy_new();

G_END_DECLS

#ifdef __cplusplus

#include <memory>

struct MessageProxyUnref
{
    void operator() (MessageProxy* messageProxy)
        { g_object_unref(messageProxy); }
};

typedef
    std::unique_ptr<
        MessageProxy,
        MessageProxyUnref> MessageProxyPtr;

inline bool operator == (const MessageProxyPtr& l, const MessageProxy* r) { return l.get() == r; }
inline bool operator < (const MessageProxyPtr& l, const MessageProxy* r) { return l.get() < r; }
inline bool operator < (const MessageProxy* l, const MessageProxyPtr& r) { return l < r.get(); }

#endif
