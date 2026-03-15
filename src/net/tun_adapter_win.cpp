#ifdef _WIN32

#include "tun_adapter_win.h"
#include "core/logger.h"
#include "core/config.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <shellapi.h>
#include <cstring>
#include <cstdio>
#include <string>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace tunngle {
namespace net {

namespace {

static const GUID TUNNGLE_ADAPTER_GUID = {
    0x3F2504E0, 0x4F89, 0x41D3,
    {0x9A, 0x0C, 0x03, 0x05, 0xE8, 0x2C, 0x33, 0x01}
};

constexpr DWORD WINTUN_RING_CAPACITY = 0x400000;  // 4 MiB

typedef void* WINTUN_ADAPTER_HANDLE;
typedef void* WINTUN_SESSION_HANDLE;

typedef WINTUN_ADAPTER_HANDLE (WINAPI *WintunCreateAdapterFunc)(LPCWSTR Name, LPCWSTR TunnelType, const GUID* RequestedGUID);
typedef void (WINAPI *WintunCloseAdapterFunc)(WINTUN_ADAPTER_HANDLE Adapter);
typedef WINTUN_SESSION_HANDLE (WINAPI *WintunStartSessionFunc)(WINTUN_ADAPTER_HANDLE Adapter, DWORD Capacity);
typedef void (WINAPI *WintunEndSessionFunc)(WINTUN_SESSION_HANDLE Session);
typedef HANDLE (WINAPI *WintunGetReadWaitEventFunc)(WINTUN_SESSION_HANDLE Session);
typedef BYTE* (WINAPI *WintunReceivePacketFunc)(WINTUN_SESSION_HANDLE Session, DWORD* PacketSize);
typedef void (WINAPI *WintunReleaseReceivePacketFunc)(WINTUN_SESSION_HANDLE Session, const BYTE* Packet);
typedef BYTE* (WINAPI *WintunAllocateSendPacketFunc)(WINTUN_SESSION_HANDLE Session, DWORD PacketSize);
typedef void (WINAPI *WintunSendPacketFunc)(WINTUN_SESSION_HANDLE Session, const BYTE* Packet);
typedef void (WINAPI *WintunGetAdapterLuidFunc)(WINTUN_ADAPTER_HANDLE Adapter, NET_LUID* Luid);

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], len);
    return out;
}

}  // namespace

TunAdapterWin::TunAdapterWin() = default;

TunAdapterWin::~TunAdapterWin() {
    Close();
    UnloadWintun();
}

bool TunAdapterWin::LoadWintun() {
    if (wintun_dll_) return true;

    wintun_dll_ = LoadLibraryExW(L"wintun.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!wintun_dll_) {
        char path[MAX_PATH];
        DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
        if (len > 0) {
            std::string dir(path, len);
            size_t slash = dir.find_last_of("\\/");
            if (slash != std::string::npos) {
                dir = dir.substr(0, slash + 1) + "wintun.dll";
                wintun_dll_ = LoadLibraryExA(dir.c_str(), nullptr, 0);
            }
        }
    }
    if (!wintun_dll_) {
        LOG_ERROR("TUN: failed to load wintun.dll (error %lu). "
                  "Download from https://www.wintun.net/ and place wintun.dll next to the executable.",
                  GetLastError());
        return false;
    }

    fn_create_adapter_           = (void*)GetProcAddress(wintun_dll_, "WintunCreateAdapter");
    fn_close_adapter_            = (void*)GetProcAddress(wintun_dll_, "WintunCloseAdapter");
    fn_start_session_            = (void*)GetProcAddress(wintun_dll_, "WintunStartSession");
    fn_end_session_              = (void*)GetProcAddress(wintun_dll_, "WintunEndSession");
    fn_get_read_wait_event_      = (void*)GetProcAddress(wintun_dll_, "WintunGetReadWaitEvent");
    fn_receive_packet_           = (void*)GetProcAddress(wintun_dll_, "WintunReceivePacket");
    fn_release_receive_packet_   = (void*)GetProcAddress(wintun_dll_, "WintunReleaseReceivePacket");
    fn_allocate_send_packet_     = (void*)GetProcAddress(wintun_dll_, "WintunAllocateSendPacket");
    fn_send_packet_              = (void*)GetProcAddress(wintun_dll_, "WintunSendPacket");
    fn_get_adapter_luid_         = (void*)GetProcAddress(wintun_dll_, "WintunGetAdapterLUID");

    if (!fn_create_adapter_ || !fn_close_adapter_ || !fn_start_session_ ||
        !fn_end_session_ || !fn_get_read_wait_event_ || !fn_receive_packet_ ||
        !fn_release_receive_packet_ || !fn_allocate_send_packet_ || !fn_send_packet_ ||
        !fn_get_adapter_luid_) {
        LOG_ERROR("TUN: wintun.dll is missing required exports");
        FreeLibrary(wintun_dll_);
        wintun_dll_ = nullptr;
        return false;
    }

    return true;
}

void TunAdapterWin::UnloadWintun() {
    if (wintun_dll_) {
        FreeLibrary(wintun_dll_);
        wintun_dll_ = nullptr;
    }
}

static bool AddRoute7_0_0_0(NET_LUID luid) {
    NET_IFINDEX ifindex = 0;
    if (ConvertInterfaceLuidToIndex(&luid, &ifindex) != NO_ERROR) return false;

    MIB_IPFORWARD_ROW2 row;
    InitializeIpForwardEntry(&row);
    row.InterfaceLuid = luid;
    row.InterfaceIndex = ifindex;
    row.DestinationPrefix.Prefix.si_family = AF_INET;
    if (inet_pton(AF_INET, "7.0.0.0", &row.DestinationPrefix.Prefix.Ipv4.sin_addr) != 1) return false;
    row.DestinationPrefix.PrefixLength = 24;
    row.NextHop.si_family = AF_INET;
    row.NextHop.Ipv4.sin_addr.s_addr = 0;  // On-link
    row.SitePrefixLength = 0;
    row.ValidLifetime = 0xFFFFFFFF;
    row.PreferredLifetime = 0xFFFFFFFF;
    row.Metric = 0;
    row.Protocol = MIB_IPPROTO_NETMGMT;
    row.Loopback = FALSE;
    row.AutoconfigureAddress = FALSE;
    row.Publish = FALSE;
    row.Immortal = FALSE;
    row.Age = 0;
    row.Origin = NlroManual;

    DWORD err = CreateIpForwardEntry2(&row);
    if (err != NO_ERROR && err != ERROR_OBJECT_ALREADY_EXISTS) {
        LOG_ERROR("TUN: CreateIpForwardEntry2(7.0.0.0/24) failed: %lu", err);
        return false;
    }
    return true;
}

bool TunAdapterWin::AssignIP(const std::string& ip) {
    NET_LUID luid;
    auto getLuid = reinterpret_cast<WintunGetAdapterLuidFunc>(fn_get_adapter_luid_);
    getLuid(adapter_, &luid);

    MIB_UNICASTIPADDRESS_ROW addr;
    InitializeUnicastIpAddressEntry(&addr);
    addr.InterfaceLuid = luid;
    addr.Address.si_family = AF_INET;
    addr.OnLinkPrefixLength = 24;  // /24 — only local subnet; peers reached via UDP tunnel
    addr.DadState = IpDadStatePreferred;

    if (inet_pton(AF_INET, ip.c_str(), &addr.Address.Ipv4.sin_addr) != 1) {
        LOG_ERROR("TUN: invalid IP address: %s", ip.c_str());
        return false;
    }

    DWORD err = CreateUnicastIpAddressEntry(&addr);
    if (err != NO_ERROR && err != ERROR_OBJECT_ALREADY_EXISTS) {
        LOG_ERROR("TUN: CreateUnicastIpAddressEntry failed: %lu", err);
        return false;
    }

    if (!AddRoute7_0_0_0(luid)) {
        LOG_WARN("TUN: route 7.0.0.0/24 not added (ping may fail); try running as Administrator");
    }
    return true;
}

bool TunAdapterWin::Open() {
    if (session_) return true;

    if (!LoadWintun()) return false;

    auto createAdapter = reinterpret_cast<WintunCreateAdapterFunc>(fn_create_adapter_);
    auto startSession = reinterpret_cast<WintunStartSessionFunc>(fn_start_session_);
    auto getReadEvent = reinterpret_cast<WintunGetReadWaitEventFunc>(fn_get_read_wait_event_);

    std::wstring wname = Utf8ToWide(ifname_);
    adapter_ = createAdapter(wname.c_str(), L"PeerDen", &TUNNGLE_ADAPTER_GUID);
    if (!adapter_) {
        LOG_ERROR("TUN: WintunCreateAdapter failed (error %lu). Run as Administrator.", GetLastError());
        return false;
    }

    session_ = startSession(adapter_, WINTUN_RING_CAPACITY);
    if (!session_) {
        DWORD err = GetLastError();
        LOG_ERROR("TUN: WintunStartSession failed (error %lu)", err);
        auto closeAdapter = reinterpret_cast<WintunCloseAdapterFunc>(fn_close_adapter_);
        closeAdapter(adapter_);
        adapter_ = nullptr;
        return false;
    }

    read_event_ = getReadEvent(session_);

    assigned_ip_ = local_ip_.empty() ? "7.0.0.1" : local_ip_;
    if (!AssignIP(assigned_ip_)) {
        LOG_ERROR("TUN: IP assignment failed for %s (requires Administrator)", assigned_ip_.c_str());
        Close();
        return false;
    }

    LOG_INFO("TUN connected: %s @ %s", ifname_.c_str(), assigned_ip_.c_str());
    return true;
}

void TunAdapterWin::Close() {
    if (session_) {
        auto endSession = reinterpret_cast<WintunEndSessionFunc>(fn_end_session_);
        endSession(session_);
        session_ = nullptr;
        read_event_ = nullptr;
    }
    if (adapter_) {
        auto closeAdapter = reinterpret_cast<WintunCloseAdapterFunc>(fn_close_adapter_);
        closeAdapter(adapter_);
        adapter_ = nullptr;
        LOG_INFO("TUN disconnected: %s", ifname_.c_str());
    }
    assigned_ip_.clear();
}

ssize_t TunAdapterWin::Read(uint8_t* buf, size_t capacity) {
    if (!session_) return -1;

    auto receivePacket = reinterpret_cast<WintunReceivePacketFunc>(fn_receive_packet_);
    auto releasePacket = reinterpret_cast<WintunReleaseReceivePacketFunc>(fn_release_receive_packet_);

    DWORD pkt_size = 0;
    BYTE* pkt = receivePacket(session_, &pkt_size);
    if (!pkt) {
        DWORD err = GetLastError();
        if (err == ERROR_NO_MORE_ITEMS) return 0;
        return -1;
    }

    if (pkt_size > capacity) {
        releasePacket(session_, pkt);
        return -1;
    }

    std::memcpy(buf, pkt, pkt_size);
    releasePacket(session_, pkt);
    return static_cast<ssize_t>(pkt_size);
}

ssize_t TunAdapterWin::Write(const uint8_t* buf, size_t len) {
    if (!session_) return -1;

    auto allocPacket = reinterpret_cast<WintunAllocateSendPacketFunc>(fn_allocate_send_packet_);
    auto sendPacket = reinterpret_cast<WintunSendPacketFunc>(fn_send_packet_);

    BYTE* pkt = allocPacket(session_, static_cast<DWORD>(len));
    if (!pkt) return -1;

    std::memcpy(pkt, buf, len);
    sendPacket(session_, pkt);
    return static_cast<ssize_t>(len);
}

std::string TunAdapterWin::GetInterfaceName() const {
    return ifname_;
}

std::string TunAdapterWin::GetAssignedIP() const {
    return assigned_ip_;
}

bool TunAdapterWin::IsOpen() const {
    return session_ != nullptr;
}

bool TunAdapterWin::RequestElevationAndRelaunch() {
    Config::Instance().ForceSave();

    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        LOG_ERROR("TUN: GetModuleFileName failed");
        return false;
    }

    std::string config_dir = Config::Instance().GetConfigDir();
    std::string params = "--config-dir=\"" + config_dir + "\"";

    SHELLEXECUTEINFOA sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "runas";
    sei.lpFile = path;
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (!ShellExecuteExA(&sei)) {
        DWORD err = GetLastError();
        if (err == ERROR_CANCELLED) {
            LOG_INFO("TUN: User cancelled elevation prompt");
        } else {
            LOG_ERROR("TUN: ShellExecuteEx failed (error %lu)", err);
        }
        return false;
    }

    ExitProcess(0);
}

}  // namespace net
}  // namespace tunngle

#endif
