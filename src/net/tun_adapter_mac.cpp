#include "tun_adapter_mac.h"
#include "core/logger.h"
#include "core/config.h"
#include <sys/socket.h>
#include <sys/sys_domain.h>
#include <sys/kern_control.h>
#include <sys/ioctl.h>
#include <net/if_utun.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace tunngle {
namespace net {

namespace {

constexpr size_t UTUN_AF_PREFIX = 4;

static size_t RoundUp4(size_t n) {
    return (n > 0 ? (1 + ((n - 1) | 3)) : 4);
}

bool AddRouteInProcess(const std::string& ifname, const std::string& dest_net, int prefix_len) {
    unsigned int ifindex = if_nametoindex(ifname.c_str());
    if (ifindex == 0) {
        LOG_ERROR("TUN AddRoute: if_nametoindex(%s) failed: %s", ifname.c_str(), strerror(errno));
        return false;
    }

    // Build sockaddr_dl manually - utun may not have AF_LINK in getifaddrs
    struct sockaddr_dl sdl;
    std::memset(&sdl, 0, sizeof(sdl));
    sdl.sdl_len = sizeof(sdl);
    sdl.sdl_family = AF_LINK;
    sdl.sdl_index = ifindex;
    sdl.sdl_nlen = 0;
    sdl.sdl_alen = 0;
    sdl.sdl_slen = 0;

    struct sockaddr_in dst, mask;
    std::memset(&dst, 0, sizeof(dst));
    std::memset(&mask, 0, sizeof(mask));
    dst.sin_family = AF_INET;
    dst.sin_len = sizeof(dst);
    if (inet_pton(AF_INET, dest_net.c_str(), &dst.sin_addr) != 1) return false;
    mask.sin_family = AF_INET;
    mask.sin_len = sizeof(mask);
    uint32_t m = (prefix_len == 32) ? 0xFFFFFFFFu : ((1u << prefix_len) - 1u) << (32 - prefix_len);
    mask.sin_addr.s_addr = htonl(m);

    struct {
        struct rt_msghdr hdr;
        char data[512];
    } rtmsg;
    std::memset(&rtmsg, 0, sizeof(rtmsg));

    rtmsg.hdr.rtm_type = RTM_ADD;
    rtmsg.hdr.rtm_flags = RTF_UP | RTF_STATIC;
    rtmsg.hdr.rtm_version = RTM_VERSION;
    rtmsg.hdr.rtm_seq = 1;
    rtmsg.hdr.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;

    char* p = rtmsg.data;
    size_t len = RoundUp4(sizeof(dst));
    std::memcpy(p, &dst, sizeof(dst));
    p += len;
    len = RoundUp4(sdl.sdl_len);
    std::memcpy(p, &sdl, sdl.sdl_len);
    p += len;
    len = RoundUp4(sizeof(mask));
    std::memcpy(p, &mask, sizeof(mask));
    p += len;

    rtmsg.hdr.rtm_msglen = static_cast<uint16_t>(p - reinterpret_cast<char*>(&rtmsg));

    int fd = socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC);
    if (fd < 0) {
        LOG_ERROR("TUN AddRoute: socket(PF_ROUTE) failed: %s", strerror(errno));
        return false;
    }
    ssize_t n = write(fd, &rtmsg, rtmsg.hdr.rtm_msglen);
    close(fd);
    if (n < 0) {
        LOG_ERROR("TUN AddRoute: write failed: %s", strerror(errno));
        return false;
    }
    LOG_INFO("TUN route added: %s/%d via %s (ifindex %u)", dest_net.c_str(), prefix_len, ifname.c_str(), ifindex);
    return true;
}

bool AssignIP(const std::string& ifname, const std::string& ip) {
    // Configure via ioctl in-process - utun may not be visible to ifconfig subprocess
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG_ERROR("TUN AssignIP: socket failed: %s", strerror(errno));
        return false;
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    if (ifname.size() >= IFNAMSIZ) {
        close(sock);
        return false;
    }
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        close(sock);
        return false;
    }
    std::memcpy(&ifr.ifr_addr, &addr, sizeof(addr));
    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
        LOG_ERROR("TUN AssignIP SIOCSIFADDR failed: %s", strerror(errno));
        close(sock);
        return false;
    }

    struct sockaddr_in mask;
    std::memset(&mask, 0, sizeof(mask));
    mask.sin_family = AF_INET;
    mask.sin_addr.s_addr = inet_addr("255.255.255.0");
    std::memcpy(&ifr.ifr_addr, &mask, sizeof(mask));
    if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0) {
        LOG_ERROR("TUN AssignIP SIOCSIFNETMASK failed: %s", strerror(errno));
        close(sock);
        return false;
    }

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        close(sock);
        return false;
    }
    ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
        LOG_ERROR("TUN AssignIP SIOCSIFFLAGS failed: %s", strerror(errno));
        close(sock);
        return false;
    }
    close(sock);

    // Add route in-process via PF_ROUTE - route subprocess may not see utun interface
    if (!AddRouteInProcess(ifname, "7.0.0.0", 24)) {
        LOG_ERROR("TUN AddRoute failed for 7.0.0.0/24 via %s", ifname.c_str());
        return false;
    }
    return true;
}

}  // namespace

TunAdapterMac::TunAdapterMac() = default;

TunAdapterMac::~TunAdapterMac() {
    Close();
}

bool TunAdapterMac::Open() {
    if (fd_ >= 0) return true;

    struct ctl_info ctlInfo;
    std::memset(&ctlInfo, 0, sizeof(ctlInfo));
    if (strlcpy(ctlInfo.ctl_name, UTUN_CONTROL_NAME, sizeof(ctlInfo.ctl_name)) >=
        sizeof(ctlInfo.ctl_name)) {
        LOG_ERROR("TUN strlcpy failed: name too long");
        return false;
    }

    fd_ = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd_ < 0) {
        LOG_ERROR("TUN socket failed: %s (errno=%d)", strerror(errno), errno);
        return false;
    }

    if (ioctl(fd_, CTLIOCGINFO, &ctlInfo) < 0) {
        LOG_ERROR("TUN ioctl(CTLIOCGINFO) failed: %s (errno=%d)", strerror(errno), errno);
        close(fd_);
        fd_ = -1;
        return false;
    }

    struct sockaddr_ctl sc;
    std::memset(&sc, 0, sizeof(sc));
    sc.sc_id = ctlInfo.ctl_id;
    sc.sc_len = sizeof(sc);
    sc.sc_family = AF_SYSTEM;
    sc.ss_sysaddr = AF_SYS_CONTROL;
    sc.sc_unit = unit_;  // Creates utunN (unit N)

    if (connect(fd_, reinterpret_cast<struct sockaddr*>(&sc), sizeof(sc)) < 0) {
        LOG_ERROR("TUN connect failed: %s (errno=%d)", strerror(errno), errno);
        close(fd_);
        fd_ = -1;
        return false;
    }

    char ifname_buf[32] = {0};
    socklen_t ifname_len = sizeof(ifname_buf);
    if (getsockopt(fd_, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, ifname_buf, &ifname_len) < 0) {
        LOG_ERROR("TUN getsockopt(UTUN_OPT_IFNAME) failed: %s (errno=%d)", strerror(errno), errno);
        close(fd_);
        fd_ = -1;
        return false;
    }
    ifname_ = ifname_buf;

    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    }

    assigned_ip_ = local_ip_.empty() ? "7.0.0.1" : local_ip_;
    if (!AssignIP(ifname_, assigned_ip_)) {
        LOG_ERROR("TUN AssignIP failed for %s (ifconfig may need elevated privileges)", ifname_.c_str());
        close(fd_);
        fd_ = -1;
        ifname_.clear();
        return false;
    }

    LOG_INFO("TUN connected: %s @ %s", ifname_.c_str(), assigned_ip_.c_str());
    return true;
}

bool DeleteRouteInProcess(const std::string& ifname, const std::string& dest_net, int prefix_len) {
    unsigned int ifindex = if_nametoindex(ifname.c_str());
    if (ifindex == 0) return false;
    struct sockaddr_dl sdl;
    std::memset(&sdl, 0, sizeof(sdl));
    sdl.sdl_len = sizeof(sdl);
    sdl.sdl_family = AF_LINK;
    sdl.sdl_index = ifindex;
    struct sockaddr_in dst, mask;
    std::memset(&dst, 0, sizeof(dst));
    std::memset(&mask, 0, sizeof(mask));
    dst.sin_family = AF_INET;
    dst.sin_len = sizeof(dst);
    if (inet_pton(AF_INET, dest_net.c_str(), &dst.sin_addr) != 1) return false;
    mask.sin_family = AF_INET;
    mask.sin_len = sizeof(mask);
    uint32_t m = (prefix_len == 32) ? 0xFFFFFFFFu : ((1u << prefix_len) - 1u) << (32 - prefix_len);
    mask.sin_addr.s_addr = htonl(m);
    struct { struct rt_msghdr hdr; char data[512]; } rtmsg;
    std::memset(&rtmsg, 0, sizeof(rtmsg));
    rtmsg.hdr.rtm_type = RTM_DELETE;
    rtmsg.hdr.rtm_flags = RTF_UP | RTF_STATIC;
    rtmsg.hdr.rtm_version = RTM_VERSION;
    rtmsg.hdr.rtm_seq = 1;
    rtmsg.hdr.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
    char* p = rtmsg.data;
    size_t len = RoundUp4(sizeof(dst));
    std::memcpy(p, &dst, sizeof(dst));
    p += len;
    len = RoundUp4(sdl.sdl_len);
    std::memcpy(p, &sdl, sdl.sdl_len);
    p += len;
    len = RoundUp4(sizeof(mask));
    std::memcpy(p, &mask, sizeof(mask));
    p += len;
    rtmsg.hdr.rtm_msglen = static_cast<uint16_t>(p - reinterpret_cast<char*>(&rtmsg));
    int fd = socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC);
    if (fd < 0) return false;
    ssize_t n = write(fd, &rtmsg, rtmsg.hdr.rtm_msglen);
    close(fd);
    return n >= 0;
}

void TunAdapterMac::Close() {
    if (fd_ >= 0) {
        if (!ifname_.empty()) {
            DeleteRouteInProcess(ifname_, "7.0.0.0", 24);
        }
        LOG_INFO("TUN disconnected: %s", ifname_.c_str());
        close(fd_);
        fd_ = -1;
    }
    ifname_.clear();
    assigned_ip_.clear();
}

ssize_t TunAdapterMac::Read(uint8_t* buf, size_t capacity) {
    if (fd_ < 0) return -1;
    if (capacity < UTUN_AF_PREFIX) return -1;

    uint8_t raw[2048];
    ssize_t n = read(fd_, raw, sizeof(raw));
    if (n <= 0) return n;
    if (static_cast<size_t>(n) <= UTUN_AF_PREFIX) return 0;

    n -= UTUN_AF_PREFIX;
    if (static_cast<size_t>(n) > capacity) return -1;
    std::memcpy(buf, raw + UTUN_AF_PREFIX, static_cast<size_t>(n));
    return n;
}

ssize_t TunAdapterMac::Write(const uint8_t* buf, size_t len) {
    if (fd_ < 0) return -1;

    uint8_t raw[2048];
    if (len + UTUN_AF_PREFIX > sizeof(raw)) return -1;

    raw[0] = 0;
    raw[1] = 0;
    raw[2] = 0;
    raw[3] = 2;  // AF_INET for IPv4
    std::memcpy(raw + UTUN_AF_PREFIX, buf, len);

    return write(fd_, raw, len + UTUN_AF_PREFIX);
}

std::string TunAdapterMac::GetInterfaceName() const {
    return ifname_;
}

std::string TunAdapterMac::GetAssignedIP() const {
    return assigned_ip_;
}

bool TunAdapterMac::IsOpen() const {
    return fd_ >= 0;
}

bool TunAdapterMac::RequestElevationAndRelaunch() {
    Config::Instance().ForceSave();

    char path[4096];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        LOG_ERROR("TUN failed to get executable path");
        return false;
    }

    char escaped[8192];
    char* out = escaped;
    const char* p = path;
    for (; *p && out < escaped + sizeof(escaped) - 4; ++p) {
        if (*p == '\'') {
            *out++ = '\'';
            *out++ = '\\';
            *out++ = '\'';
            *out++ = '\'';
        } else {
            *out++ = *p;
        }
    }
    *out = '\0';

    std::string config_dir = Config::Instance().GetConfigDir();
    char config_escaped[4096];
    char* cout = config_escaped;
    for (char ch : config_dir) {
        if (ch == '\'') {
            *cout++ = '\'';
            *cout++ = '\\';
            *cout++ = '\'';
            *cout++ = '\'';
        } else {
            *cout++ = ch;
        }
    }
    *cout = '\0';

    char script[16384];
    snprintf(script, sizeof(script),
             "do shell script \"exec '%s' '--config-dir=%s'\" with administrator privileges",
             escaped, config_escaped);

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("TUN fork failed: %s", strerror(errno));
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }

    execl("/usr/bin/osascript", "osascript", "-e", script, nullptr);
    LOG_ERROR("TUN execl osascript failed: %s", strerror(errno));
    _exit(1);
}

}  // namespace net
}  // namespace tunngle
