#pragma once

#include "core/platform.h"
#include <cstddef>
#include <cstdint>

namespace tunngle {
namespace net {

class Tunnel {
public:
    virtual ~Tunnel() = default;
    virtual bool IsConnected() const = 0;
    virtual bool IsConnecting() const = 0;
    virtual void Pump(uint64_t now_ms) = 0;
    virtual ssize_t Send(const uint8_t* data, size_t len) = 0;
    virtual ssize_t Receive(uint8_t* buf, size_t capacity) = 0;
    virtual void Disconnect() = 0;
    virtual bool TimedOut() const { return false; }
    /// Last measured RTT in ms. Returns -1 if unknown (not connected or no pong yet).
    virtual int GetPingMs() const { return -1; }
};

}  // namespace net
}  // namespace tunngle
