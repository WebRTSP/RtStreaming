#include "Helpers.h"

#include <netdb.h>
#include <arpa/inet.h>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>


namespace GstRtStreaming
{

IceServerType ParseIceServerType(const std::string& iceServer)
{
    if(0 == iceServer.compare(0, 5, "turn:"))
        return IceServerType::Turn;
    else if(0 == iceServer.compare(0, 6, "turns:"))
        return IceServerType::Turns;
    else if(0 == iceServer.compare(0, 5, "stun:"))
        return IceServerType::Stun;
    else
        return IceServerType::Unknown;
}

bool IsMDNSResolveRequired()
{
    guint vMajor = 0, vMinor = 0;
    gst_plugins_base_version(&vMajor, &vMinor, nullptr, nullptr);

    return vMajor == 1 && vMinor < 18;
}

bool IsEndOfCandidatesSupported()
{
    //"end-of-candidates" support was added only after GStreamer 1.18
    guint gstMajor = 0, gstMinor = 0;
    gst_plugins_base_version(&gstMajor, &gstMinor, 0, 0);

    return gstMajor > 1 || (gstMajor == 1 && gstMinor > 18);
}

bool IsAddTurnServerSupported()
{
    guint vMajor = 0, vMinor = 0;
    gst_plugins_base_version(&vMajor, &vMinor, nullptr, nullptr);

    return vMajor > 1 || (vMajor == 1 && vMinor >= 16);
}

bool IsIceGatheringStateBroken()
{
    // "ice-gathering-state" is broken in GStreamer < 1.17.1
    guint gstMajor = 0, gstMinor = 0, gstNano = 0;
    gst_plugins_base_version(&gstMajor, &gstMinor, &gstNano, 0);

    return
        (gstMajor == 1 && gstMinor < 17) ||
        (gstMajor == 1 && gstMinor == 17 && gstNano == 0);
}

bool IsIceAgentAvailable()
{
    guint gstMajor = 0, gstMinor = 0, gstNano = 0;
    gst_plugins_base_version(&gstMajor, &gstMinor, &gstNano, 0);

    return
        gstMajor > 1 ||
        (gstMajor == 1 && gstMinor > 17) ||
        (gstMajor == 1 && gstMinor == 17 && gstNano > 0);
}

void TryResolveMDNSIceCandidate(
    const std::string& candidate,
    std::string* resolvedCandidate)
{
    if(!resolvedCandidate)
        return;

    resolvedCandidate->clear();

    if(candidate.empty())
        return;

    enum {
        ConnectionAddressPos = 4,
        PortPos = 5,
    };

    std::string::size_type pos = 0;
    unsigned token = 0;

    std::string::size_type connectionAddressPos = std::string::npos;
    std::string::size_type portPos = std::string::npos;
    while(std::string::npos == portPos &&
        (pos = candidate.find(' ', pos)) != std::string::npos)
    {
        ++pos;
        ++token;

        switch(token) {
        case ConnectionAddressPos:
            connectionAddressPos = pos;
            break;
        case PortPos:
            portPos = pos;
            break;
        }
    }

    if(connectionAddressPos != std::string::npos &&
        portPos != std::string::npos)
    {
        std::string connectionAddress(
            candidate, connectionAddressPos,
            portPos - connectionAddressPos - 1);

        const std::string::value_type localSuffix[] = ".local";
        if(connectionAddress.size() > (sizeof(localSuffix) - 1) &&
           connectionAddress.compare(
               connectionAddress.size() - (sizeof(localSuffix) - 1),
               std::string::npos,
               localSuffix) == 0)
        {
            if(hostent* host = gethostbyname(connectionAddress.c_str())) {
                int ipLen;
                switch(host->h_addrtype) {
                case AF_INET:
                    ipLen = INET_ADDRSTRLEN;
                    break;
                case AF_INET6:
                    ipLen = INET6_ADDRSTRLEN;
                    break;
                default:
                    return;
                }

                char ip[ipLen];
                if(nullptr == inet_ntop(
                    host->h_addrtype,
                    host->h_addr_list[0],
                    ip, ipLen))
                {
                   return;
                }

                if(resolvedCandidate) {
                    ipLen = strlen(ip);

                    *resolvedCandidate = candidate.substr(0, connectionAddressPos);
                    resolvedCandidate->append(ip, ipLen);
                    resolvedCandidate->append(
                        candidate,
                        portPos - 1,
                        std::string::npos);
                }
            }
        }
    }
}

}
