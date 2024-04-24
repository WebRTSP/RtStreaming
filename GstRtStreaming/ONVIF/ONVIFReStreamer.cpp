#include "ONVIFReStreamer.h"

#include <gsoap/plugin/wsseapi.h>

#include "ONVIF/DeviceBinding.nsmap"
#include "ONVIF/soapDeviceBindingProxy.h"
#include "ONVIF/soapMediaBindingProxy.h"
#include "ONVIF/soapPullPointSubscriptionBindingProxy.h"

#include "CxxPtr/GlibPtr.h"
#include "CxxPtr/GioPtr.h"


namespace {

enum Error: int32_t {
    DEVICE_MEDIA_HAS_NO_PROFILES = 1,
    DEVICE_MEDIA_PROFILE_HAS_NO_STREAM_URI = 2,
    NOTIFICATION_MESSAGE_HAS_NO_DATA_ELEMENT = 3,
    NOTIFICATION_MESSAGE_DOES_NOT_CONTAIN_MOTION_EVENT = 4,
};

struct RequestMediaUrisTaskData
{
    const std::string sourceUrl;
    const std::optional<std::string> username;
    const std::optional<std::string> password;
};

void AddAuth(
    struct soap* soap,
    const std::optional<std::string>& username,
    const std::optional<std::string>& password) noexcept
{
    if(!username && !password) return;

    soap_wsse_add_UsernameTokenDigest(
        soap,
        nullptr,
        username ? username->c_str() : "",
        password ? password->c_str() : "");
}

}

struct ONVIFReStreamer::Private
{
    static GQuark SoapDomain;
    static GQuark Domain;

    static void requestMediaUrisTaskFunc(
        GTask* task,
        gpointer sourceObject,
        gpointer taskData,
        GCancellable* cancellable);

    struct MediaUris {
        std::string streamUri;
    };

    Private(
        ONVIFReStreamer* owner,
        const std::string sourceUrl,
        const std::optional<std::string>& username,
        const std::optional<std::string>& password) noexcept;

    void requestMediaUris() noexcept;
    void onMediaUris(std::unique_ptr<MediaUris>&) noexcept;
    void onMediaUrisRequestFailed() noexcept;

    ONVIFReStreamer *const owner;

    std::shared_ptr<spdlog::logger> log;

    const std::string sourceUrl;
    const std::optional<std::string> username;
    const std::optional<std::string> password;

    GCancellablePtr mediaUrlRequestTaskCancellablePtr;
    GTaskPtr mediaUrlRequestTaskPtr;

    std::unique_ptr<MediaUris> mediaUris;
};

GQuark ONVIFReStreamer::Private::SoapDomain = g_quark_from_static_string("ONVIFReStreamer::SOAP");
GQuark ONVIFReStreamer::Private::Domain = g_quark_from_static_string("ONVIFReStreamer");

ONVIFReStreamer::Private::Private(
    ONVIFReStreamer* owner,
    const std::string sourceUrl,
    const std::optional<std::string>& username,
    const std::optional<std::string>& password) noexcept :
    owner(owner),
    log(GstRtStreamingLog()),
    sourceUrl(sourceUrl),
    username(username),
    password(password)
{
}

void ONVIFReStreamer::Private::requestMediaUrisTaskFunc(
    GTask* task,
    gpointer sourceObject,
    gpointer taskData,
    GCancellable* cancellable)
{
    const RequestMediaUrisTaskData* data = static_cast<RequestMediaUrisTaskData*>(taskData);

    soap_status status;


    DeviceBindingProxy deviceProxy(data->sourceUrl.c_str());

    _tds__GetCapabilities getCapabilities;
    _tds__GetCapabilitiesResponse getCapabilitiesResponse;
    AddAuth(deviceProxy.soap, data->username, data->password);
    status = deviceProxy.GetCapabilities(&getCapabilities, getCapabilitiesResponse);
    if(status != SOAP_OK) {
        const char* faultString = soap_fault_string(deviceProxy.soap);
        GError* error = g_error_new_literal(SoapDomain, status, faultString ? faultString : "GetCapabilities failed");
        g_task_return_error(task, error);
        return;
    }

    const std::string& mediaEndpoint = getCapabilitiesResponse.Capabilities->Media->XAddr;


    MediaBindingProxy mediaProxy(mediaEndpoint.c_str());

    _trt__GetProfiles getProfiles;
    _trt__GetProfilesResponse getProfilesResponse;
    AddAuth(mediaProxy.soap, data->username, data->password);
    status = mediaProxy.GetProfiles(&getProfiles, getProfilesResponse);
    if(status != SOAP_OK) {
        const char* faultString = soap_fault_string(deviceProxy.soap);
        GError* error = g_error_new_literal(SoapDomain, status, faultString ? faultString : "GetProfiles failed");
        g_task_return_error(task, error);
        return;
    }

    if(getProfilesResponse.Profiles.size() == 0) {
        GError* error =
            g_error_new_literal(
                Domain,
                DEVICE_MEDIA_HAS_NO_PROFILES,
                "Device Media has no profiles");
        g_task_return_error(task, error);
        return;
    }

    const tt__Profile *const mediaProfile = getProfilesResponse.Profiles[0];

    _trt__GetStreamUri getStreamUri;
    _trt__GetStreamUriResponse getStreamUriResponse;
    getStreamUri.ProfileToken = mediaProfile->token;

    tt__StreamSetup streamSetup;

    tt__Transport transport;
    transport.Protocol = tt__TransportProtocol::RTSP;

    streamSetup.Transport = &transport;

    getStreamUri.StreamSetup = &streamSetup;

    AddAuth(mediaProxy.soap, data->username, data->password);
    status = mediaProxy.GetStreamUri(&getStreamUri, getStreamUriResponse);
    if(status != SOAP_OK) {
        const char* faultString = soap_fault_string(deviceProxy.soap);
        GError* error = g_error_new_literal(SoapDomain, status, faultString ? faultString : "GetStreamUri failed");
        g_task_return_error(task, error);
        return;
    }

    const tt__MediaUri *const mediaUri = getStreamUriResponse.MediaUri;
    if(!mediaUri || mediaUri->Uri.empty()) {
        GError* error =
            g_error_new_literal(
                Domain,
                DEVICE_MEDIA_PROFILE_HAS_NO_STREAM_URI,
                "Device Media Profile has no stream uri");
        g_task_return_error(task, error);
        return;
    }

    GCharPtr uriStringPtr;
    if(data->username || data->password) {
        GUriPtr uriPtr(g_uri_parse(mediaUri->Uri.c_str(), G_URI_FLAGS_ENCODED, nullptr));
        GUri* uri = uriPtr.get();
        if(!g_uri_get_user(uri) && !g_uri_get_password(uri)) {
            GCharPtr userPtr(
                data->username ?
                    g_uri_escape_string(
                        data->username->c_str(),
                        G_URI_RESERVED_CHARS_SUBCOMPONENT_DELIMITERS,
                        false) :
                        nullptr);
            GCharPtr passwordPtr(
                data->password ?
                    g_uri_escape_string(
                        data->password->c_str(),
                        G_URI_RESERVED_CHARS_SUBCOMPONENT_DELIMITERS,
                        false) :
                        nullptr);
            uriStringPtr.reset(
                g_uri_join_with_user(
                    G_URI_FLAGS_ENCODED,
                    g_uri_get_scheme(uri),
                    userPtr.get(),
                    passwordPtr.get(),
                    g_uri_get_auth_params(uri),
                    g_uri_get_host(uri),
                    g_uri_get_port(uri),
                    g_uri_get_path(uri),
                    g_uri_get_query(uri),
                    g_uri_get_fragment(uri)));
        }
    }

    const std::string& mediaUriUri = uriStringPtr ? std::string(uriStringPtr.get()) : mediaUri->Uri;

    g_task_return_pointer(
        task,
        new MediaUris { mediaUriUri },
        [] (gpointer mediaUris) { delete(static_cast<MediaUris*>(mediaUris)); });
}

void ONVIFReStreamer::Private::requestMediaUris() noexcept
{
    auto readyCallback =
        [] (GObject* sourceObject, GAsyncResult* result, gpointer userData) {
            g_return_if_fail(g_task_is_valid(result, sourceObject));

            GError* error = nullptr;
            MediaUris* mediaUris =
                reinterpret_cast<MediaUris*>(g_task_propagate_pointer(G_TASK(result), &error));
            GErrorPtr errorPtr(error);
            std::unique_ptr<MediaUris> mediaUrisPtr(mediaUris);
            if(errorPtr) {
                GstRtStreamingLog()->error(
                    "[{}] {}",
                    g_quark_to_string(errorPtr->domain),
                    errorPtr->message);

                if(errorPtr->code != G_IO_ERROR_CANCELLED) {
                    // has error but not cancelled (i.e. owner is still available)
                    ONVIFReStreamer::Private* self =
                        reinterpret_cast<ONVIFReStreamer::Private*>(userData);
                    self->onMediaUrisRequestFailed();
                }
            }

            ONVIFReStreamer::Private* self =
                reinterpret_cast<ONVIFReStreamer::Private*>(userData);

            if(mediaUrisPtr) {
                // no error and not cancelled yet
                self->onMediaUris(mediaUrisPtr);
            } else {
                self->onMediaUrisRequestFailed();
            }
        };

    GCancellable* cancellable = g_cancellable_new();
    GTask* task = g_task_new(nullptr, cancellable, readyCallback, this);
    mediaUrlRequestTaskCancellablePtr.reset(cancellable);
    mediaUrlRequestTaskPtr.reset(task);

    g_task_set_return_on_cancel(task, true);
    g_task_set_task_data(
        task,
        new RequestMediaUrisTaskData { sourceUrl, username, password },
        [] (gpointer sourceUrl) { delete(static_cast<RequestMediaUrisTaskData*>(sourceUrl)); });

    g_task_run_in_thread(task, requestMediaUrisTaskFunc);
}

void ONVIFReStreamer::Private::onMediaUris(std::unique_ptr<MediaUris>& mediaUris) noexcept
{
    log->info("Media stream uri discovered: {}", mediaUris->streamUri);

    owner->setSourceUrl(mediaUris->streamUri);

    this->mediaUris.swap(mediaUris);

    const bool prepared = owner->GstReStreamer2::prepare();
    assert(prepared);
}

void ONVIFReStreamer::Private::onMediaUrisRequestFailed() noexcept
{
    owner->onEos(true);
}


ONVIFReStreamer::ONVIFReStreamer(
    const std::string& sourceUrl,
    const std::string& forceH264ProfileLevelId,
    const std::optional<std::string>& username,
    const std::optional<std::string>& password) noexcept :
    GstReStreamer2(forceH264ProfileLevelId),
    _p(std::make_unique<Private>(this, sourceUrl, username, password))
{
}

ONVIFReStreamer::~ONVIFReStreamer()
{
    if(_p->mediaUrlRequestTaskCancellablePtr) {
        g_cancellable_cancel(_p->mediaUrlRequestTaskCancellablePtr.get());
    }
}

bool ONVIFReStreamer::prepare() noexcept
{
    assert(!pipeline());
    if(pipeline())
        return true;

    _p->requestMediaUris();

    return true;
}

void ONVIFReStreamer::cleanup() noexcept
{
    if(_p->mediaUrlRequestTaskCancellablePtr) {
        g_cancellable_cancel(_p->mediaUrlRequestTaskCancellablePtr.get());
        _p->mediaUrlRequestTaskCancellablePtr.reset();
    }

    _p->mediaUris.reset();

    GstStreamingSource::cleanup();
}
