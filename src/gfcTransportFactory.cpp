#include "gfcTransportFactory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gfcRakNetTransport.h"

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
        // JEF-24 Task 3 provides WebRtcTransport; falling back to RakNet.
        std::printf("[net] JEFECHECK_TRANSPORT=webrtc requested, but "
                    "WebRtcTransport is not available yet (JEF-24 Task 3); "
                    "falling back to RakNet\n");
    }
    return std::make_unique<RakNetTransport>();
}

} // namespace net
} // namespace jefe
