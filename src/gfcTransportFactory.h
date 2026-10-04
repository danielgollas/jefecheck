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
// SessionPolicy is a plain aggregate declared with the coordinator wire
// encoders; including the header here keeps one definition rather than a
// parallel struct that could drift from the wire format.
#include "gfcCoordinatorSignaling.h"

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
    // JEF-31: coordinator access JWT. Empty = anonymous; the coordinator
    // answers auth-required if it gates create-session, and the caller runs
    // sign-in then retries.
    std::string authToken;
    // JEF-37: nickname shown to the host in the admit prompt when knocking.
    std::string displayName;
    // Host-side session policy (knock/password/timeout/cap) from the selected
    // session group. Ignored for joiners — only a host sends it.
    SessionPolicy policy;
    /**
     * JEF-37: shared secret that lets the host's OWN loopback client skip the
     * lobby.
     *
     * A cloud host also connects to its own session as a client (that loopback
     * is how the host mirrors state). With knocking on, that client is just
     * another joiner — so the host ends up waiting to admit itself, and until
     * it does the session has no participants at all.
     *
     * The host generates a random nonce, hosts with it, and hands the same
     * value to its loopback as that client's COORDINATOR-level display name
     * (the app-level nickname, and so the participant list, is untouched). A
     * knock whose display name matches is admitted without ever reaching the
     * UI. Everyone else still knocks.
     *
     * Deliberately not a coordinator-side rule: this works against a
     * coordinator already deployed, and against an anonymous self-hosted one
     * where there is no identity to match on.
     */
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
