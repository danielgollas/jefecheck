#include "gfcTransportFactory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gfcRakNetTransport.h"
#ifdef JEFECHECK_WEBRTC
#include "gfcWebRtcTransport.h"
#endif

namespace jefe {
namespace net {

TransportKind transportKindFromEnv() {
    const char* v = std::getenv("JEFECHECK_TRANSPORT");
    if (v && std::strcmp(v, "webrtc") == 0)
        return TransportKind::WebRtc;
    return TransportKind::RakNet;
}

std::unique_ptr<ITransport> makeTransport(TransportKind kind) {
    if (kind == TransportKind::WebRtc) {
#ifdef JEFECHECK_WEBRTC
        return std::make_unique<WebRtcTransport>();
#else
        // Built without WebRTC support: fall back to RakNet.
        std::printf("[net] JEFECHECK_TRANSPORT=webrtc requested, but this build "
                    "has no WebRTC support (JEFECHECK_WEBRTC=OFF); falling back "
                    "to RakNet\n");
#endif
    }
    return std::make_unique<RakNetTransport>();
}

} // namespace net
} // namespace jefe
