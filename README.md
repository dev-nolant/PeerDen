# PeerDen

P2P virtual LAN for gaming over the internet. Create lobbies, invite friends, and play any LAN-enabled game together.

## Getting Started

1. **Download** the installer from [peerden.io](https://peerden.io) for your platform (Windows or macOS).
2. **Install** and run PeerDen. On first launch, Windows may show a security prompt—click "More info" then "Run anyway".
3. **Create an account** by clicking Account in the top-right and signing up.
4. **Join or create a lobby** in the Community tab. Browse public rooms or make your own.
5. **Launch your game** and select LAN multiplayer. The game will use the virtual LAN adapter automatically.

For help, visit the [forums](https://forums.peerden.io).

Consider [supporting me on Ko-fi](https://ko-fi.com/devnt) if PeerDen is useful to you.

## Building from Source

### Prerequisites

- CMake 3.20+
- C++17 compiler (Visual Studio 2022 on Windows, GCC/Clang on macOS/Linux)
- Python 3.x (for icon conversion during build)
- OpenSSL 3.x (optional, for TLS coord server; tested with OpenSSL-Win64 from [slproweb.com](https://slproweb.com/products/Win32OpenSSL.html))

### Windows

1. Download `wintun.dll` from [wintun.net](https://www.wintun.net/) and place it in the project root.
2. Build:

```powershell
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DOPENSSL_ROOT_DIR="C:\Program Files\OpenSSL-Win64"
cmake --build . --config Release
```

Output: `build\Release\peerdden.exe` (with `wintun.dll` and OpenSSL DLLs copied alongside). The installer bundles these—end users don't need to install OpenSSL.

### macOS

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Output: `build/peerdden`

### Linux

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Output: `build/peerdden`

## Project Structure

- `src/` – C++ client (ImGui, GLFW, networking, TUN adapter)
- `assets/` – Icons and images
- `installer/` – Inno Setup script for Windows installer
- `scripts/` – Build helpers (icon conversion, macOS packaging)

## Third-Party Components

This project uses the following third-party software. All are compatible with the project's MIT license:

| Component | License | Notes |
|-----------|---------|------|
| [GLFW](https://github.com/glfw/glfw) | zlib | Windowing and input |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT | UI framework |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | MIT | HTTP client |
| [stb_image](https://github.com/nothings/stb) | MIT / Public Domain | Image loading |
| [OpenSSL](https://www.openssl.org/) | OpenSSL License | TLS (optional) |
| [Wintun](https://www.wintun.net/) | [Prebuilt Binaries License](https://git.zx2c4.com/wintun/tree/prebuilt-binaries-license.txt) | TUN driver for Windows. `wintun.dll` is distributed unmodified from WireGuard LLC. Download from [wintun.net](https://www.wintun.net/) and place in project root before building. |

The coordination and API server remain private for multiple reasons, but mainly because it's a bit concerning exposing infra code to the public. I am open to privately exchanging various parts, but it will never be fully released (atleast while PeerDen is up and running). What data is used and how is outlined in our [Terms](https://peerden.io/terms) and [Privacy](https://peerden.io/privacy) policies.

## License

See [LICENSE](LICENSE) for details.
