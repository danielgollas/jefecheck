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
    // Matches legacy behavior: peers were never destroyed (empty dtors,
    // gfcnetworkclient.cpp:75 / gfcnetworkserver.cpp:76). Keep identical
    // lifetime semantics for zero behavior change; revisit in JEF-23.
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
            // Legacy behavior was log-and-continue (discard the tampered
            // packet and keep draining the queue). Loop form per reviewer
            // preference instead of recursing.
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
            // App packet (>= GFCNET_USER_PACKET_BASE) or unknown system id.
            // Deliver raw bytes; unknown ids fall through app switches' default
            // cases exactly as before.
            ev.type = TransportEventType::Data;
            ev.bytes.assign(p->data, p->data + p->length);
            break;
        }
        peer_->DeallocatePacket(p);
        return true;
    }
    return false;
}

void RakNetTransport::send(const unsigned char* data, int len, PeerId target,
                           bool broadcastExcluding) {
    SystemAddress addr = (target == kInvalidPeerId)
                             ? UNASSIGNED_SYSTEM_ADDRESS
                             : toSystemAddress(target);
    peer_->Send((const char*)data, len, HIGH_PRIORITY, RELIABLE_ORDERED, 0,
                addr, broadcastExcluding);
}

void RakNetTransport::closePeer(PeerId peer, bool sendNotification) {
    peer_->CloseConnection(toSystemAddress(peer), sendNotification);
}

int RakNetTransport::connectionCount() {
    return (int)peer_->NumberOfConnections();
}

} // namespace net
} // namespace jefe
