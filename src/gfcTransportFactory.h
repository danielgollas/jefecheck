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

#include "gfcTransport.h"

namespace jefe {
namespace net {

enum class TransportKind { RakNet, WebRtc };

// Reads JEFECHECK_TRANSPORT once per call; "webrtc" -> WebRtc, else RakNet.
TransportKind transportKindFromEnv();

std::unique_ptr<ITransport> makeTransport(TransportKind kind = transportKindFromEnv());

} // namespace net
} // namespace jefe

#endif
