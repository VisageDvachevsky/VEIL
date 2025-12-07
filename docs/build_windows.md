# Building VEIL on Windows

## Prerequisites

### Required Tools

1. **Visual Studio 2022** (Community Edition or higher)
   - Workloads: "Desktop development with C++"
   - Components: MSVC v143, Windows 11 SDK, CMake tools

2. **CMake** 3.20 or later
   - Included with Visual Studio, or download from [cmake.org](https://cmake.org/download/)

3. **Git for Windows**
   - Download from [git-scm.com](https://git-scm.com/download/win)

4. **Qt6** (6.5 or later)
   - Download from [qt.io](https://www.qt.io/download-qt-installer)
   - Required components: Qt 6.5+ for MSVC 2022 64-bit
   - Install to `C:\Qt\6.5.3\msvc2022_64` (or note custom path)

5. **vcpkg** (for dependencies)
   ```powershell
   git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
   cd C:\vcpkg
   .\bootstrap-vcpkg.bat
   .\vcpkg integrate install
   ```

### Required Dependencies

Install via vcpkg:

```powershell
cd C:\vcpkg
.\vcpkg install libsodium:x64-windows
.\vcpkg install spdlog:x64-windows
.\vcpkg install fmt:x64-windows
.\vcpkg install cli11:x64-windows
.\vcpkg install nlohmann-json:x64-windows
```

### Wintun SDK

1. Download Wintun SDK from [wintun.net](https://www.wintun.net/)
2. Extract to `C:\wintun` (or custom location)
3. Note the path for CMake configuration

## Building

### Option 1: Visual Studio GUI

1. **Open project in Visual Studio:**
   - File → Open → CMake
   - Select `CMakeLists.txt` from VEIL root directory

2. **Configure CMake:**
   - Tools → Options → CMake → CMake variables
   - Add:
     ```
     CMAKE_TOOLCHAIN_FILE = C:/vcpkg/scripts/buildsystems/vcpkg.cmake
     Qt6_DIR = C:/Qt/6.5.3/msvc2022_64/lib/cmake/Qt6
     WINTUN_ROOT = C:/wintun
     ```

3. **Build:**
   - Build → Build All
   - Or select specific target (veil-client, veil-server, veil-gui)

### Option 2: Command Line (CMake + Ninja)

```powershell
# Set up environment
$env:Qt6_DIR = "C:\Qt\6.5.3\msvc2022_64\lib\cmake\Qt6"
$env:CMAKE_TOOLCHAIN_FILE = "C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
$env:WINTUN_ROOT = "C:\wintun"

# Configure
cmake -B build -G "Ninja" `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:CMAKE_TOOLCHAIN_FILE" `
  -DQt6_DIR="$env:Qt6_DIR" `
  -DWINTUN_ROOT="$env:WINTUN_ROOT"

# Build
cmake --build build --config Release

# Output: build/Release/veil-client.exe, etc.
```

### Option 3: Visual Studio Command Line (MSBuild)

```powershell
# Open "x64 Native Tools Command Prompt for VS 2022"

# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DQt6_DIR=C:\Qt\6.5.3\msvc2022_64\lib\cmake\Qt6 ^
  -DWINTUN_ROOT=C:\wintun

# Build
cmake --build build --config Release

# Output: build\Release\veil-client.exe, etc.
```

## Build Targets

### Core Executables

- **veil-client.exe** - Command-line VPN client
- **veil-server.exe** - VPN server
- **VEILService.exe** - Windows service (runs client/server as background service)

### GUI Applications

- **veil-gui-client.exe** - Qt-based client GUI
- **veil-gui-server.exe** - Qt-based server management GUI

### Optional Targets

- **veil-tests.exe** - Unit and integration tests (requires `-DVEIL_BUILD_TESTS=ON`)

## CMake Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `VEIL_BUILD_TESTS` | ON | Build unit and integration tests |
| `VEIL_BUILD_TOOLS` | ON | Build utility tools |
| `VEIL_BUILD_GUI` | ON | Build Qt GUI applications |
| `CMAKE_BUILD_TYPE` | Release | Build type (Release/Debug/RelWithDebInfo) |
| `WINTUN_ROOT` | (required) | Path to Wintun SDK |

## Platform Detection

The build system automatically detects Windows and includes Windows-specific sources:

```cmake
if(WIN32)
  set(VEIL_TUN_IMPL src/tun/tun_device_windows.cpp)
  set(VEIL_IPC_IMPL src/common/ipc/ipc_socket_windows.cpp)
  set(VEIL_ROUTING_IMPL src/tun/routing_windows.cpp)

  target_link_libraries(veil_common PRIVATE iphlpapi ws2_32)
else()
  set(VEIL_TUN_IMPL src/tun/tun_device_linux.cpp)
  set(VEIL_IPC_IMPL src/common/ipc/ipc_socket_unix.cpp)
  set(VEIL_ROUTING_IMPL src/tun/routing_linux.cpp)
endif()
```

## Post-Build Steps

### Copying Dependencies

After building, copy required DLLs to output directory:

```powershell
# Qt DLLs (adjust path based on your Qt installation)
Copy-Item C:\Qt\6.5.3\msvc2022_64\bin\Qt6Core.dll build\Release\
Copy-Item C:\Qt\6.5.3\msvc2022_64\bin\Qt6Gui.dll build\Release\
Copy-Item C:\Qt\6.5.3\msvc2022_64\bin\Qt6Widgets.dll build\Release\

# Wintun DLL
Copy-Item C:\wintun\bin\amd64\wintun.dll build\Release\

# Visual C++ Runtime (if not using static runtime)
# Usually handled automatically by Qt or Visual Studio
```

Or use Qt's `windeployqt` tool:

```powershell
cd build\Release
C:\Qt\6.5.3\msvc2022_64\bin\windeployqt.exe veil-gui-client.exe
```

### Installing VEILService

To install the Windows service:

```powershell
# Run as Administrator
cd build\Release
.\VEILService.exe --install

# Start service
sc start VEILService

# Check status
sc query VEILService
```

## Running

### Command-Line Client

```powershell
cd build\Release
.\veil-client.exe --config client.conf
```

### GUI Client

```powershell
cd build\Release
.\veil-gui-client.exe
```

The GUI will connect to VEILService via Named Pipes.

## Troubleshooting

### Qt not found

**Error:** `Could not find a package configuration file provided by "Qt6"`

**Solution:**
- Ensure Qt6 is installed
- Set `Qt6_DIR` environment variable or CMake variable
- Example: `-DQt6_DIR=C:\Qt\6.5.3\msvc2022_64\lib\cmake\Qt6`

### Wintun not found

**Error:** `Could not find Wintun SDK`

**Solution:**
- Download Wintun SDK from wintun.net
- Set `WINTUN_ROOT` CMake variable
- Example: `-DWINTUN_ROOT=C:\wintun`

### vcpkg dependencies not found

**Error:** `Could not find libsodium`, etc.

**Solution:**
- Ensure vcpkg is installed and integrated
- Run `vcpkg integrate install`
- Set `CMAKE_TOOLCHAIN_FILE`
- Install dependencies: `vcpkg install libsodium:x64-windows`, etc.

### "Windows Stub Not Implemented" Errors at Runtime

**Current Status:** Windows implementation is stubbed out. You'll see errors like:

```
ERROR: Windows TUN device not yet implemented
ERROR: Windows routing not yet implemented
```

**Explanation:** Phase A (Platform Abstraction) is complete, but Windows-specific implementation (Phases B-F) requires:
- Wintun integration
- IP Helper API implementation
- Named Pipes IPC
- Windows Service framework

See TODO comments in:
- `src/tun/tun_device_windows.cpp`
- `src/tun/routing_windows.cpp`
- `src/common/ipc/ipc_socket_windows.cpp` (when implemented)

## Development

### Code Style

- Follow existing codebase style
- Use clang-format (config in `.clang-format`)
- Run before committing:
  ```powershell
  clang-format -i src/**/*.cpp src/**/*.h
  ```

### Testing

```powershell
cd build
ctest -C Release --output-on-failure
```

## Creating Installer

### Using NSIS

1. Install NSIS from [nsis.sourceforge.io](https://nsis.sourceforge.io/)
2. Create installer script (see `packaging/windows/installer.nsi`)
3. Build installer:
   ```powershell
   makensis packaging\windows\installer.nsi
   ```

### Using WiX Toolset

1. Install WiX from [wixtoolset.org](https://wixtoolset.org/)
2. Create MSI package (see `packaging/windows/veil.wxs`)

## Current Implementation Status

| Component | Status | Notes |
|-----------|--------|-------|
| Platform Abstraction | ✅ Complete | Headers and dispatcher in place |
| Windows TUN (Wintun) | 🚧 Stub | Requires wintun SDK integration |
| Windows Routing | 🚧 Stub | Requires IP Helper API implementation |
| Windows IPC | ⏳ Pending | Named Pipes implementation needed |
| Windows Service | ⏳ Pending | Service framework needed |
| Qt6 GUI | ✅ Works | Cross-platform, works on Windows |
| DPI Bypass Modes | ✅ Complete | Cross-platform, ready to use |

**Legend:**
- ✅ Complete and working
- 🚧 Stub implementation (builds but not functional)
- ⏳ Pending implementation

## Next Steps for Windows Development

1. **Integrate Wintun SDK**
   - Implement functions in `src/tun/tun_device_windows.cpp`
   - Test adapter creation and packet I/O

2. **Implement IP Helper API Routing**
   - Complete `src/tun/routing_windows.cpp`
   - Test route addition/removal

3. **Implement Named Pipes IPC**
   - Create `src/common/ipc/ipc_socket_windows.cpp`
   - Test GUI-to-service communication

4. **Create Windows Service**
   - Implement service entry point
   - Test installation and startup

5. **Integration Testing**
   - End-to-end connection test
   - Routing verification
   - GUI integration

## References

- [CMake Documentation](https://cmake.org/documentation/)
- [Visual Studio CMake Projects](https://docs.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio)
- [Qt for Windows](https://doc.qt.io/qt-6/windows.html)
- [vcpkg Documentation](https://vcpkg.io/en/getting-started.html)
- [Wintun Documentation](https://www.wintun.net/)
