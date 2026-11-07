#ifndef GFCTRANSPORTFACTORY_H
#define GFCTRANSPORTFACTORY_H

// Runtime transport selection; the ONE networking TU allowed to include concrete
// transport headers. Env JEFECHECK_TRANSPORT ("webrtc" -> WebRtc, otherwise RakNet).

#include <memory>
#include <string>

#include "gfcTransport.h"
// SessionPolicy is a plain aggregate declared with the coordinator wire
// encoders; including the header here keeps one definition rather than a
// parallel struct that could drift from the wire format.
#include "gfcCoordinatorSignaling.h"

namespace jefe {
namespace net {

enum class TransportKind { RakNet, WebRtc };

// Reads JEFECHECK_TRANSPORT once per call; "webrtc" -> WebRtc, else RakNet.
TransportKind transportKindFromEnv();

// Full transport-selection config. Default-constructed: RakNet, or LAN-WebRTC via
// JEFECHECK_TRANSPORT; coordinatorMode (or a coordinatorUrl) dials the cloud coordinator.
struct TransportConfig {
    TransportKind kind = transportKindFromEnv();
    bool coordinatorMode = false;
    std::string coordinatorUrl;   // ws:// or wss:// coordinator endpoint
    std::string sessionCode;      // join-only (the code to join); host: empty
    std::string password;
    // Coordinator access JWT. Empty = anonymous; the coordinator answers
    // auth-required if it gates create-session, and the caller signs in and retries.
    std::string authToken;
    // Nickname shown to the host in the admit prompt when knocking.
    std::string displayName;
    // Host-side session policy (knock/password/timeout/cap) from the selected
    // session group. Ignored for joiners — only a host sends it.
    SessionPolicy policy;
    // Shared secret that lets the host's OWN loopback client skip the lobby: it is
    // sent as the client's coordinator-level display name and admitted without a knock.
    std::string selfJoinNonce;
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
