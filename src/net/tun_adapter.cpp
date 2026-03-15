#include "tun_adapter.h"

#if defined(__APPLE__)
#include "tun_adapter_mac.h"
#elif defined(_WIN32)
#include "tun_adapter_win.h"
#endif

namespace tunngle {
namespace net {

std::unique_ptr<TunAdapter> TunAdapter::Create() {
#if defined(__APPLE__)
    return std::make_unique<TunAdapterMac>();
#elif defined(_WIN32)
    return std::make_unique<TunAdapterWin>();
#else
    return nullptr;
#endif
}

}  // namespace net
}  // namespace tunngle
