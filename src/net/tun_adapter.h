#pragma once

#include "core/platform.h"
#include <cstddef>
#include <memory>
#include <string>

namespace tunngle {
namespace net {

/// Abstract interface for a TUN virtual network adapter.
/// Platform-specific implementations (macOS utun, Windows TAP).
class TunAdapter {
public:
    virtual ~TunAdapter() = default;

    /// Create platform-specific adapter. Returns nullptr on unsupported platform.
    static std::unique_ptr<TunAdapter> Create();

    /// Set local IP before Open (default 7.0.0.1).
    virtual void SetLocalIP(const std::string& ip) { (void)ip; }

    /// Set utun unit before Open (macOS: 0=utun0, 1=utun1). Default 0.
    virtual void SetUnit(uint32_t unit) { (void)unit; }

    /// Open the TUN interface and assign IP. Returns false on failure.
    virtual bool Open() = 0;

    /// Close the interface.
    virtual void Close() = 0;

    /// Read one IP packet. Returns bytes read, 0 on would-block, -1 on error.
    /// Buffer must hold at least MTU (typically 1500) bytes.
    virtual ssize_t Read(uint8_t* buf, size_t capacity) = 0;

    /// Write one IP packet. Returns bytes written, -1 on error.
    virtual ssize_t Write(const uint8_t* buf, size_t len) = 0;

    /// Interface name (e.g. "utun3").
    virtual std::string GetInterfaceName() const = 0;

    /// Assigned IPv4 address (e.g. "7.0.0.1").
    virtual std::string GetAssignedIP() const = 0;

    virtual bool IsOpen() const = 0;

    /// True if this platform can prompt for elevated privileges (admin/sudo).
    virtual bool SupportsElevationPrompt() const { return false; }

    /// Request elevation and relaunch the app. On success, does not return (process exits).
    /// Returns false if not supported or elevation not needed.
    virtual bool RequestElevationAndRelaunch() { return false; }
};

}  // namespace net
}  // namespace tunngle
