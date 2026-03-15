#pragma once

#include "tun_adapter.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace tunngle {
namespace net {

/// macOS utun implementation.
class TunAdapterMac : public TunAdapter {
public:
    TunAdapterMac();
    ~TunAdapterMac() override;

    void SetLocalIP(const std::string& ip) override { local_ip_ = ip; }
    void SetUnit(uint32_t unit) override { unit_ = unit; }
    bool Open() override;
    void Close() override;
    ssize_t Read(uint8_t* buf, size_t capacity) override;
    ssize_t Write(const uint8_t* buf, size_t len) override;
    std::string GetInterfaceName() const override;
    std::string GetAssignedIP() const override;
    bool IsOpen() const override;

    bool SupportsElevationPrompt() const override { return true; }
    bool RequestElevationAndRelaunch() override;

private:
    int fd_ = -1;
    uint32_t unit_ = 0;
    std::string ifname_;
    std::string assigned_ip_;
    std::string local_ip_ = "7.0.0.1";
};

}  // namespace net
}  // namespace tunngle
