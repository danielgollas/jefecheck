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

std::unique_ptr<ITransport> makeTransport(const TransportConfig& configIn) {
    TransportConfig cfg = configIn;

    // Env fallback (headless tests select coordinator mode without a UI). The
    // config struct wins; env only fills what the caller left empty.
    if (cfg.coordinatorUrl.empty()) {
        if (const char* u = std::getenv("JEFECHECK_COORDINATOR_URL"))
            cfg.coordinatorUrl = u;
    }
    if (cfg.sessionCode.empty()) {
        if (const char* c = std::getenv("JEFECHECK_SESSION_CODE"))
            cfg.sessionCode = c;
    }
    // A coordinator URL implies coordinator mode; coordinator mode is WebRTC.
    if (!cfg.coordinatorUrl.empty()) cfg.coordinatorMode = true;
    if (cfg.coordinatorMode) cfg.kind = TransportKind::WebRtc;

    if (cfg.kind == TransportKind::WebRtc) {
#ifdef JEFECHECK_WEBRTC
        auto t = std::make_unique<WebRtcTransport>();
        if (cfg.coordinatorMode) {
            t->configureCoordinator(cfg.coordinatorUrl, cfg.sessionCode,
                                    cfg.password);
        }
        return t;
#else
        // Built without WebRTC support: fall back to RakNet.
        std::printf("[net] WebRTC transport requested, but this build has no "
                    "WebRTC support (JEFECHECK_WEBRTC=OFF); falling back to "
                    "RakNet\n");
#endif
    }
    return std::make_unique<RakNetTransport>();
}

std::unique_ptr<ITransport> makeTransport(TransportKind kind) {
    TransportConfig cfg;
    cfg.kind = kind;
    return makeTransport(cfg);
}

std::unique_ptr<ITransport> makeTransport() {
    return makeTransport(TransportConfig{});
}

} // namespace net
} // namespace jefe
