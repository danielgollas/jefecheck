#include "gfcRakNetTransport.h"

#include "RakPeerInterface.h"
#include "RakNetworkFactory.h"
#include "MessageIdentifiers.h"
#include "RakNetTypes.h"
#include "gfcNetworkStructures.h"

static_assert(ID_USER_PACKET_ENUM == GFCNET_USER_PACKET_BASE,
              "GFCNET_USER_PACKET_BASE must equal RakNet ID_USER_PACKET_ENUM");

namespace jefe {
namespace net {

static PeerId toPeerId(const SystemAddress& a) {
    return packPeerId(a.binaryAddress, a.port);
}
static SystemAddress toSystemAddress(PeerId id) {
    SystemAddress a;
    a.binaryAddress = static_cast<unsigned int>(id >> 16);
    a.port = static_cast<unsigned short>(id & 0xFFFF);
    return a;
}

RakNetTransport::RakNetTransport()
    : peer_(RakNetworkFactory::GetRakPeerInterface()) {}

RakNetTransport::~RakNetTransport() {
    // Peers are never destroyed (empty dtors in the client/server), so lifetime
    // semantics are kept identical.
}

bool RakNetTransport::startHost(unsigned short port, const std::string& password,
                                int maxClients) {
    stopHost();
    peer_->SetIncomingPassword(password.c_str(), (int)password.size());
    SocketDescriptor socketDescriptor(port, 0);
    bool ok = peer_->Startup((unsigned short)maxClients, 15, &socketDescriptor, 1);
    if (ok) peer_->SetMaximumIncomingConnections((unsigned short)maxClients);
    hosting_ = ok;
    return ok;
}

void RakNetTransport::stopHost() {
    peer_->Shutdown(30);
    hosting_ = false;
}

bool RakNetTransport::connect(const std::string& ip, unsigned short port,
                              const std::string& password) {
    peer_->Shutdown(30);
    hosting_ = false;
    SocketDescriptor socketDescriptor;
    socketDescriptor.port = 0;
    peer_->Startup(1, 15, &socketDescriptor, 1);
    return peer_->Connect(ip.c_str(), port, password.c_str(),
                          (int)password.size(), 0);
}

void RakNetTransport::disconnect() {
    peer_->CloseConnection(peer_->GetSystemAddressFromIndex(0), true, 0);
    peer_->Shutdown(30);
}

bool RakNetTransport::poll(TransportEvent& ev) {
    Packet* p = peer_->Receive();
    while (p) {
        if (p->data[0] == ID_MODIFIED_PACKET) {
            // Log-and-continue: discard the tampered packet and keep draining.
            peer_->DeallocatePacket(p);
            p = peer_->Receive();
            continue;
        }

        ev.peer = toPeerId(p->systemAddress);
        ev.bytes.clear();
        switch (p->data[0]) {
        case ID_CONNECTION_REQUEST_ACCEPTED: ev.type = TransportEventType::ConnectAccepted; break;
        case ID_CONNECTION_ATTEMPT_FAILED:   ev.type = TransportEventType::ConnectFailed;   break;
        case ID_NO_FREE_INCOMING_CONNECTIONS:ev.type = TransportEventType::ServerFull;      break;
        case ID_ALREADY_CONNECTED:           ev.type = TransportEventType::AlreadyConnected;break;
        case ID_NEW_INCOMING_CONNECTION:     ev.type = TransportEventType::PeerConnected;   break;
        case ID_DISCONNECTION_NOTIFICATION:
            ev.type = hosting_ ? TransportEventType::PeerDisconnected
                               : TransportEventType::Disconnected;
            break;
        case ID_CONNECTION_LOST:
            ev.type = hosting_ ? TransportEventType::PeerLost
                               : TransportEventType::ConnectionLost;
            break;
        default:
            // App packet or unknown system id: strip the RakNet envelope byte added by
            // send(); a packet under 2 bytes has no frame, so drop it.
            if (p->length < 2) {
                peer_->DeallocatePacket(p);
                p = peer_->Receive();
                continue;
            }
            ev.type = TransportEventType::Data;
            ev.bytes.assign(p->data + 1, p->data + p->length);
            break;
        }
        peer_->DeallocatePacket(p);
        return true;
    }
    return false;
}

void RakNetTransport::send(const unsigned char* data, int len, PeerId target,
                           bool broadcastExcluding, Channel channel) {
    SystemAddress addr = (target == kInvalidPeerId)
                             ? UNASSIGNED_SYSTEM_ADDRESS
                             : toSystemAddress(target);
    // `data` is a jefe::wire frame. Prepend the RakNet envelope byte
    // (GFCNET_USER_PACKET_BASE == 91); the real msgType is in the frame header.
    std::vector<unsigned char> packet;
    packet.reserve((size_t)len + 1);
    packet.push_back(GFCNET_USER_PACKET_BASE);
    packet.insert(packet.end(), data, data + len);
    // Channel::Assets maps to RakNet ordering channel 1 so bulk bodies don't block
    // state traffic; poll() reports State on receive (dispatch is by GFCNETID).
    const char orderingChannel = (channel == Channel::Assets) ? 1 : 0;
    peer_->Send((const char*)packet.data(), (int)packet.size(), HIGH_PRIORITY,
                RELIABLE_ORDERED, orderingChannel, addr, broadcastExcluding);
}

void RakNetTransport::closePeer(PeerId peer, bool sendNotification) {
    peer_->CloseConnection(toSystemAddress(peer), sendNotification);
}

int RakNetTransport::connectionCount() {
    return (int)peer_->NumberOfConnections();
}

// RakNet has no WebRTC stats API: emit one basic PeerStats per connected
// system (connected=true, path=Unknown, rttMs=-1, bytes=0), keyed by PeerId.
std::vector<PeerStats> RakNetTransport::peerStats() {
    std::vector<PeerStats> out;
    unsigned short count = 0;
    // First query the count with a null buffer, then fetch into a sized vector.
    if (!peer_->GetConnectionList(nullptr, &count) || count == 0) return out;
    std::vector<SystemAddress> systems(count);
    const unsigned short cap = count;   // buffer capacity for the fetch
    unsigned short filled = cap;
    if (!peer_->GetConnectionList(systems.data(), &filled)) return out;
    // GetConnectionList writes at most `cap` entries but reports the true count
    // in `filled`; clamp so a late connect cannot overrun the buffer.
    const unsigned short n = filled < cap ? filled : cap;
    out.reserve(n);
    for (unsigned short i = 0; i < n; ++i) {
        PeerStats ps;
        ps.peer = toPeerId(systems[i]);
        ps.connected = true;   // present in the connection list == connected
        ps.path = PeerStats::Path::Unknown;
        ps.rttMs = -1;
        out.push_back(ps);
    }
    return out;
}

} // namespace net
} // namespace jefe
