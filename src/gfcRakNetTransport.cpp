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
            // App packet or unknown system id. Since JEF-23, ITransport
            // carries FRAMES ([u8 version][u16 msgType LE][payload]); the
            // leading RakNet envelope byte (GFCNET_USER_PACKET_BASE, added
            // by send() below) is stripped here before the frame is emitted.
            // A packet shorter than 2 bytes is a bare envelope byte with no
            // frame — malformed; drop it and keep draining the queue.
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
    // JEF-23: `data` is a jefe::wire frame. Prepend the single RakNet
    // envelope byte so RakNet routes the packet as user data (every app
    // packet arrives with first byte GFCNET_USER_PACKET_BASE == 91 — RakNet
    // only needs one user id; the real msgType lives in the frame header).
    // One copy per send is fine at our message rates.
    std::vector<unsigned char> packet;
    packet.reserve((size_t)len + 1);
    packet.push_back(GFCNET_USER_PACKET_BASE);
    packet.insert(packet.end(), data, data + len);
    // JEF-28: map Channel::Assets → RakNet ordering channel 1 (a second
    // reliable-ordered stream) so bulk asset bodies don't head-of-line-block
    // state traffic on channel 0. On the RECEIVE side RakNet doesn't cheaply
    // hand back the ordering channel, so poll() leaves TransportEvent.channel
    // at State for RakNet — the QoS separation is best-effort here (legacy
    // transport); dispatch is by GFCNETID regardless, so this is harmless.
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

} // namespace net
} // namespace jefe
