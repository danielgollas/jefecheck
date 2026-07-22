#ifndef GFCTRANSPORTFACTORY_H
#define GFCTRANSPORTFACTORY_H

// JEF-24: runtime transport selection. The factory is the ONE networking TU
// allowed to include concrete transport headers (gfcRakNetTransport.h and,
// from Task 3, gfcWebRtcTransport.h); client/server construct via
// makeTransport() and stay decoupled from the implementations.
//
// Selection: env JEFECHECK_TRANSPORT ("webrtc" -> WebRtc, anything else or
// unset -> RakNet). RakNet remains the default — zero regression.

#include <memory>
#include <string>

#include "gfcTransport.h"

namespace jefe {
namespace net {

enum class TransportKind { RakNet, WebRtc };

// Reads JEFECHECK_TRANSPORT once per call; "webrtc" -> WebRtc, else RakNet.
TransportKind transportKindFromEnv();

// JEF-27: full transport-selection config. The default-constructed value
// (kind from env, coordinatorMode=false, empty strings) reproduces the JEF-24
// behavior exactly (RakNet default / LAN-WebRTC via JEFECHECK_TRANSPORT).
// When coordinatorMode is on (or coordinatorUrl is non-empty), the WebRTC
// transport is selected and dials the cloud coordinator instead of a LAN
// SignalingServer/Client.
struct TransportConfig {
    TransportKind kind = transportKindFromEnv();
    bool coordinatorMode = false;
    std::string coordinatorUrl;   // ws:// or wss:// coordinator endpoint
    std::string sessionCode;      // join-only (the code to join); host: empty
    std::string password;
};

// Legacy overloads (kept working; delegate to the config form).
std::unique_ptr<ITransport> makeTransport(TransportKind kind);
std::unique_ptr<ITransport> makeTransport();

// Config-driven selection. Reads JEFECHECK_COORDINATOR_URL / JEFECHECK_SESSION_CODE
// as a fallback so headless tests can select coordinator mode via the env; the
// config struct is the primary path. A non-empty coordinatorUrl implies
// coordinatorMode and forces TransportKind::WebRtc.
std::unique_ptr<ITransport> makeTransport(const TransportConfig& config);

} // namespace net
} // namespace jefe

#endif
