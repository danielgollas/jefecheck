#ifndef GFCTESTCOORDINATOR_H
#define GFCTESTCOORDINATOR_H

// Test-double coordinator for --coord-test (see jefecheck-coordinator src/protocol.ts).
// Must NOT include Qt/GL headers (developer_notes §1); rtc/* headers live only in the .cpp.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace jefe {
namespace net {

class TestCoordinator {
public:
    TestCoordinator();
    ~TestCoordinator();

    TestCoordinator(const TestCoordinator&) = delete;
    TestCoordinator& operator=(const TestCoordinator&) = delete;

    // Bind a WebSocketServer on an ephemeral port. Returns false on failure.
    bool start();
    // The bound port (0 until start() succeeds).
    uint16_t port() const;
    // Full dial URL for the coordinator: "ws://127.0.0.1:<port>/".
    std::string url() const;
    // Stop the server and drop all client sockets. Idempotent.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace net
} // namespace jefe

#endif
