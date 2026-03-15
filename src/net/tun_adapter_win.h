#pragma once

#include "tun_adapter.h"
#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace tunngle {
namespace net {

class TunAdapterWin : public TunAdapter {
public:
    TunAdapterWin();
    ~TunAdapterWin() override;

    void SetLocalIP(const std::string& ip) override { local_ip_ = ip; }
    void SetUnit(uint32_t unit) override { (void)unit; }
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
    bool LoadWintun();
    void UnloadWintun();
    bool AssignIP(const std::string& ip);

    HMODULE wintun_dll_ = nullptr;
    void* adapter_ = nullptr;
    void* session_ = nullptr;
    HANDLE read_event_ = nullptr;
    std::string ifname_ = "PeerDen";
    std::string assigned_ip_;
    std::string local_ip_ = "7.0.0.1";

    // WinTun function pointers
    void* fn_create_adapter_ = nullptr;
    void* fn_close_adapter_ = nullptr;
    void* fn_start_session_ = nullptr;
    void* fn_end_session_ = nullptr;
    void* fn_get_read_wait_event_ = nullptr;
    void* fn_receive_packet_ = nullptr;
    void* fn_release_receive_packet_ = nullptr;
    void* fn_allocate_send_packet_ = nullptr;
    void* fn_send_packet_ = nullptr;
    void* fn_get_adapter_luid_ = nullptr;
};

}  // namespace net
}  // namespace tunngle

#endif
