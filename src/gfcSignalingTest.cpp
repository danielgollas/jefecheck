// JEF-24 Task 2: headless loopback self-test for the signaling stub
// (--signal-test). Kept in its own TU so the rtc lifecycle bracketing
// (Preload/Cleanup) and <chrono>/<condition_variable> live away from the
// production wrappers.
#include "gfcSignaling.h"

#include <rtc/rtc.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>

namespace jefe {
namespace net {

int signalingSelfTest() {
    // Bracket every rtc object with Preload/Cleanup or the process segfaults
    // at exit (spike-verified). InitLogger quiets libdatachannel.
    rtc::InitLogger(rtc::LogLevel::Error);
    rtc::Preload();

    int pass = 0;
    int fail = 0;
    auto check = [&](bool cond, const char* what) {
        if (cond) { ++pass; }
        else { ++fail; std::fprintf(stderr, "SIGNAL-TEST fail: %s\n", what); }
    };

    // Shared state for the async round-trip.
    std::mutex mtx;
    std::condition_variable cv;
    bool serverGotHello = false;
    bool clientGotOffer = false;
    bool serverSawConnect = false;
    SignalMessage serverRx;
    SignalMessage clientRx;

    {
        SignalingServer server;
        SignalingClient client;

        server.onClientConnected([&](int /*id*/) {
            std::lock_guard<std::mutex> lk(mtx);
            serverSawConnect = true;
        });

        server.onMessage([&](int clientId, const std::string& json) {
            SignalMessage m;
            parseSignal(json, m);
            if (m.type == "hello") {
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    serverRx = m;
                    serverGotHello = true;
                }
                cv.notify_all();
                // Reply with an offer carrying a synthetic peer id.
                SignalMessage offer;
                offer.type = "offer";
                offer.sdp = "v=0\r\no=- 42 2 IN IP4 127.0.0.1\r\n";  // stub SDP
                offer.peer = 7;
                server.sendTo(clientId, encodeSignal(offer));
            }
        });

        client.onMessage([&](const std::string& json) {
            SignalMessage m;
            parseSignal(json, m);
            if (m.type == "offer") {
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    clientRx = m;
                    clientGotOffer = true;
                }
                cv.notify_all();
            }
        });

        client.onOpen([&]() {
            SignalMessage hello;
            hello.type = "hello";
            hello.peer = 0;
            client.send(encodeSignal(hello));
        });

        // Ephemeral port: 0 → OS assigns; read it back.
        bool started = server.start(0);
        check(started, "server.start(0)");
        uint16_t port = server.boundPort();
        check(port != 0, "boundPort != 0");

        if (started && port != 0) {
            bool connected = client.connect("127.0.0.1", port);
            check(connected, "client.connect");

            // Poll with a bounded 3s timeout: WebSocket connect is async.
            std::unique_lock<std::mutex> lk(mtx);
            bool ok = cv.wait_for(lk, std::chrono::seconds(3), [&] {
                return serverGotHello && clientGotOffer;
            });
            lk.unlock();
            check(ok, "round-trip within 3s");
        }

        // Verify contents both directions.
        check(serverGotHello, "server received hello");
        check(serverRx.type == "hello", "server rx type == hello");
        check(serverSawConnect, "server saw client connect");
        check(clientGotOffer, "client received offer");
        check(clientRx.type == "offer", "client rx type == offer");
        check(clientRx.peer == 7, "client rx peer == 7");
        check(!clientRx.sdp.empty(), "client rx sdp non-empty");

        // Encoder/parser round-trip sanity (candidate + escaping).
        SignalMessage c;
        c.type = "candidate";
        c.candidate = "candidate:1 1 udp 1 127.0.0.1 5000 typ host \"q\\z\"";
        c.mid = "0";
        c.peer = 3;
        SignalMessage back;
        bool p = parseSignal(encodeSignal(c), back);
        check(p, "candidate round-trip parses");
        check(back.candidate == c.candidate, "candidate escaping round-trips");
        check(back.mid == "0" && back.peer == 3, "candidate mid/peer round-trip");

        // Malformed input must not crash and must return false-ish safely.
        SignalMessage junk;
        parseSignal("{ this is not json", junk);
        parseSignal("", junk);
        parseSignal("}}}", junk);
        check(true, "malformed input handled without crash");

        // server/client destruct here (before Cleanup).
    }

    std::printf("SIGNAL-TEST: pass=%d fail=%d\n", pass, fail);
    std::fflush(stdout);

    rtc::Cleanup();
    return fail == 0 ? 0 : 2;
}

}  // namespace net
}  // namespace jefe
