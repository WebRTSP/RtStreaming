#include "GstPipelineOwner.h"

#include <glib.h>


// thread safe
void GstPipelineOwner::PostLog(
    GstElement* element,
    spdlog::level::level_enum level,
    const std::string& logMessage)
{
    g_autoptr(GstBus) bus = gst_element_get_bus(element);
    if(!bus)
        return;

    GValue messageValue = G_VALUE_INIT;
    g_value_init(&messageValue, G_TYPE_STRING);
    g_value_take_string(&messageValue, g_strdup(logMessage.c_str()));

    GstStructure* structure =
        gst_structure_new_empty("log");

    gst_structure_set(
        structure,
        "level", G_TYPE_INT, level,
        NULL);

    gst_structure_take_value(
        structure,
        "message",
        &messageValue);

    GstMessage* message =
        gst_message_new_application(GST_OBJECT(element), structure);

    gst_bus_post(bus, message);
}

