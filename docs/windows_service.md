# Windows Service Implementation (VEILService)

## Overview

VEILService is a Windows service that hosts the VEIL VPN core logic, manages the TUN device, handles routing, and provides IPC to the GUI client.

## Architecture

```
┌───────────────────────────────────────┐
│ veil-gui.exe (User-level GUI)         │
│ - Connection UI                        │
│ - Settings management                  │
│ - Status display                       │
└────────────┬──────────────────────────┘
             │ Named Pipes IPC
┌────────────▼──────────────────────────┐
│ VEILService.exe (Windows Service)     │
│ - Runs as LocalSystem or NetworkService│
│ - Owns wintun adapter                  │
│ - Manages routing table                │
│ - Runs VEIL core logic                 │
│ - Handles reconnection                 │
└────────────┬──────────────────────────┘
             │ Wintun API
┌────────────▼──────────────────────────┐
│ wintun.sys (Kernel Driver)            │
│ - Virtual network adapter              │
└───────────────────────────────────────┘
```

## Service Characteristics

### Service Configuration

```cpp
SERVICE_TABLE_ENTRY ServiceTable[] = {
    { L"VEILService", ServiceMain },
    { nullptr, nullptr }
};

void WINAPI ServiceMain(DWORD argc, LPWSTR* argv) {
    // Register service control handler
    g_StatusHandle = RegisterServiceCtrlHandlerW(
        L"VEILService",
        ServiceControlHandler
    );

    // Report initial status
    SetServiceStatus(SERVICE_START_PENDING);

    // Perform initialization
    if (!Initialize()) {
        SetServiceStatus(SERVICE_STOPPED);
        return;
    }

    // Report running status
    SetServiceStatus(SERVICE_RUNNING);

    // Run main service loop
    ServiceLoop();

    // Cleanup and stop
    Cleanup();
    SetServiceStatus(SERVICE_STOPPED);
}
```

### Service Properties

| Property | Value | Rationale |
|----------|-------|-----------|
| **Service Name** | `VEILService` | Internal identifier |
| **Display Name** | `VEIL VPN Service` | User-visible name |
| **Description** | `Provides secure VPN connectivity with traffic obfuscation` | Shown in services.msc |
| **Start Type** | `SERVICE_AUTO_START` | Start automatically on boot |
| **Account** | `NT AUTHORITY\LocalService` or `NetworkService` | Minimal privileges |
| **Dependencies** | `Tcpip`, `Dhcp` | Network stack must be ready |
| **Error Control** | `SERVICE_ERROR_NORMAL` | Log errors but continue boot |

## Service Lifecycle

### Installation

```cpp
bool InstallService() {
    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) return false;

    WCHAR path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    SC_HANDLE service = CreateServiceW(
        scm,
        L"VEILService",                    // Service name
        L"VEIL VPN Service",               // Display name
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,                // Auto-start
        SERVICE_ERROR_NORMAL,
        path,                              // Binary path
        nullptr,                           // No load ordering group
        nullptr,                           // No tag ID
        L"Tcpip\0Dhcp\0",                 // Dependencies
        L"NT AUTHORITY\\NetworkService",  // Service account
        nullptr                            // No password
    );

    if (!service) {
        CloseServiceHandle(scm);
        return false;
    }

    // Set description
    SERVICE_DESCRIPTION desc = {
        L"Provides secure VPN connectivity with DPI bypass and traffic obfuscation"
    };
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &desc);

    // Set failure actions (restart on crash)
    SC_ACTION actions[] = {
        { SC_ACTION_RESTART, 5000 },   // Restart after 5 seconds
        { SC_ACTION_RESTART, 10000 },  // Restart after 10 seconds
        { SC_ACTION_NONE, 0 }          // Give up after 2 failures
    };
    SERVICE_FAILURE_ACTIONSW failureActions = {
        60,        // Reset failure count after 60 seconds
        nullptr,   // No reboot message
        nullptr,   // No command to run
        3,         // Number of actions
        actions
    };
    ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failureActions);

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}
```

### Uninstallation

```cpp
bool UninstallService() {
    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE service = OpenServiceW(scm, L"VEILService", SERVICE_STOP | DELETE);
    if (!service) {
        CloseServiceHandle(scm);
        return false;
    }

    // Stop service if running
    SERVICE_STATUS status;
    ControlService(service, SERVICE_CONTROL_STOP, &status);

    // Wait for stop (up to 30 seconds)
    for (int i = 0; i < 30 && status.dwCurrentState != SERVICE_STOPPED; i++) {
        Sleep(1000);
        QueryServiceStatus(service, &status);
    }

    // Delete service
    bool success = DeleteService(service);

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return success;
}
```

### Starting/Stopping

```cpp
bool StartVEILService() {
    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE service = OpenServiceW(scm, L"VEILService", SERVICE_START);
    if (!service) {
        CloseServiceHandle(scm);
        return false;
    }

    bool success = StartServiceW(service, 0, nullptr);

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return success;
}

bool StopVEILService() {
    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE service = OpenServiceW(scm, L"VEILService", SERVICE_STOP);
    if (!service) {
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS status;
    bool success = ControlService(service, SERVICE_CONTROL_STOP, &status);

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return success;
}
```

## Service Responsibilities

### 1. TUN Device Management

```cpp
class VEILServiceCore {
 public:
  bool Initialize() {
    // Create/open wintun adapter
    tun_device_ = std::make_unique<TunDevice>();
    TunConfig config{
      .device_name = "VEIL",
      .ip_address = "10.8.0.2",
      .netmask = "255.255.255.0",
      .mtu = 1400,
      .bring_up = true
    };

    std::error_code ec;
    if (!tun_device_->open(config, ec)) {
      LOG_ERROR("Failed to create TUN device: {}", ec.message());
      return false;
    }

    return true;
  }

 private:
  std::unique_ptr<TunDevice> tun_device_;
};
```

### 2. Routing Management

```cpp
  bool ConfigureRouting(const std::string& server_ip) {
    route_manager_ = std::make_unique<RouteManager>();

    std::error_code ec;

    // Save current routing state
    if (!route_manager_->save_routes(ec)) {
      LOG_WARN("Failed to save current routes");
    }

    // Add route to VPN server via default gateway (avoid routing loop)
    Route server_route{
      .destination = server_ip,
      .netmask = "255.255.255.255",
      .gateway = original_default_gateway_,
      .interface = original_default_interface_,
      .metric = 1
    };
    route_manager_->add_route(server_route, ec);

    // Add default route via VPN
    route_manager_->add_default_route("VEIL", "10.8.0.1", 1, ec);

    return true;
  }
```

### 3. IPC Server

```cpp
  bool StartIPC() {
    ipc_server_ = std::make_unique<IpcServer>("\\\\.\\pipe\\veil-service");

    std::error_code ec;
    if (!ipc_server_->start(ec)) {
      LOG_ERROR("Failed to start IPC server: {}", ec.message());
      return false;
    }

    // Set message handler
    ipc_server_->on_message([this](const Message& msg, int client_fd) {
      HandleIPCMessage(msg, client_fd);
    });

    return true;
  }

  void HandleIPCMessage(const Message& msg, int client_fd) {
    if (msg.type == MessageType::kCommand) {
      const Command& cmd = std::get<Command>(msg.payload);

      switch (cmd.type) {
        case CommandType::kConnect:
          HandleConnect(cmd, client_fd);
          break;
        case CommandType::kDisconnect:
          HandleDisconnect(cmd, client_fd);
          break;
        case CommandType::kGetStatus:
          HandleGetStatus(cmd, client_fd);
          break;
        // ... other commands
      }
    }
  }
```

### 4. Main Service Loop

```cpp
void ServiceLoop() {
  while (service_running_) {
    // 1. Process IPC messages
    std::error_code ec;
    ipc_server_->poll(ec);

    // 2. Process TUN packets (if connected)
    if (tunnel_ && tunnel_->is_connected()) {
      tunnel_->process_events(10 /* timeout_ms */);
    }

    // 3. Check for service control events
    if (service_stop_requested_) {
      break;
    }

    // 4. Periodic tasks
    if (auto now = std::chrono::steady_clock::now();
        now - last_status_broadcast_ > std::chrono::seconds(5)) {
      BroadcastStatus();
      last_status_broadcast_ = now;
    }
  }
}
```

### 5. Graceful Shutdown

```cpp
void Cleanup() {
  LOG_INFO("VEILService shutting down...");

  // 1. Disconnect tunnel
  if (tunnel_) {
    tunnel_->disconnect();
    tunnel_.reset();
  }

  // 2. Stop IPC server
  if (ipc_server_) {
    ipc_server_->stop();
    ipc_server_.reset();
  }

  // 3. Restore routing
  if (route_manager_) {
    std::error_code ec;
    route_manager_->restore_routes(ec);
    route_manager_.reset();
  }

  // 4. Close TUN device
  if (tun_device_) {
    tun_device_->close();
    tun_device_.reset();
  }

  LOG_INFO("VEILService stopped cleanly");
}
```

## Crash Recovery

### Automatic Restart

Configured via `SERVICE_FAILURE_ACTIONS`:
- First failure: Restart after 5 seconds
- Second failure: Restart after 10 seconds
- Third+ failures: Manual intervention required

### Cleanup on Crash

Since wintun adapters and routes persist, service must clean up on restart:

```cpp
bool CleanupFromPreviousCrash() {
  std::error_code ec;

  // 1. Check for existing VEIL adapter
  auto adapter = WintunOpenAdapter(L"VEIL");
  if (adapter) {
    LOG_WARN("Found existing VEIL adapter from previous run");
    // Reuse it or delete and recreate
  }

  // 2. Check for orphaned routes
  RouteManager temp_mgr;
  auto state = temp_mgr.get_system_state(ec);
  if (state) {
    // Look for routes pointing to VEIL adapter
    // Remove any that shouldn't be there
  }

  return true;
}
```

## Logging

### Log Destination

```
%PROGRAMDATA%\VEIL\logs\VEILService.log
```

Example: `C:\ProgramData\VEIL\logs\VEILService.log`

### Log Configuration

```cpp
void ConfigureLogging() {
  // Create log directory
  std::filesystem::path log_dir = GetProgramDataPath() / "VEIL" / "logs";
  std::filesystem::create_directories(log_dir);

  // Configure spdlog
  auto log_file = log_dir / "VEILService.log";
  auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      log_file.string(),
      1024 * 1024 * 10,  // 10 MB max size
      3                  // Keep 3 rotated files
  );

  auto logger = std::make_shared<spdlog::logger>("VEILService", file_sink);
  logger->set_level(spdlog::level::info);
  logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

  spdlog::set_default_logger(logger);
}
```

### Windows Event Log (Optional)

For integration with Windows Event Viewer:

```cpp
void WriteToEventLog(const std::string& message, WORD type) {
  HANDLE hEventLog = RegisterEventSourceW(nullptr, L"VEILService");
  if (hEventLog) {
    const WCHAR* strings[] = {
      std::wstring(message.begin(), message.end()).c_str()
    };
    ReportEventW(hEventLog, type, 0, 0, nullptr, 1, 0, strings, nullptr);
    DeregisterEventSource(hEventLog);
  }
}
```

## Security

### Privilege Separation

- Service runs as `NetworkService` (limited privileges)
- Can access network but not full system
- Can modify routing table (required capability)
- Cannot access user files

### IPC Security

- Named pipe with restricted ACL
- Only allow connections from:
  - Same user (for user-level service)
  - Local administrators
  - Local service account

```cpp
SECURITY_ATTRIBUTES CreatePipeSecurityAttributes() {
  SECURITY_ATTRIBUTES sa = {};
  sa.nLength = sizeof(sa);

  // Create SECURITY_DESCRIPTOR
  SECURITY_DESCRIPTOR sd;
  InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);

  // Allow Everyone to connect (authentication happens at protocol level)
  // Or restrict to administrators + current user
  // TODO: Implement proper ACL

  sa.lpSecurityDescriptor = &sd;
  return sa;
}
```

## TODO Items

- [ ] Implement Windows service entry point (ServiceMain)
- [ ] Add service control handler (ServiceControlHandler)
- [ ] Implement service installation/uninstallation functions
- [ ] Create main service loop integrating TUN, routing, and IPC
- [ ] Add crash recovery and cleanup logic
- [ ] Configure logging to %PROGRAMDATA%\VEIL\logs
- [ ] Implement privilege checking (ensure admin on install)
- [ ] Add Windows Event Log integration
- [ ] Create service installer (MSI or setup.exe)
- [ ] Test service auto-start on boot
- [ ] Test graceful shutdown and cleanup
- [ ] Validate crash recovery (kill process, check cleanup on restart)
- [ ] Ensure service can be controlled via services.msc

## References

- [Windows Services Documentation](https://docs.microsoft.com/en-us/windows/win32/services/services)
- [Service Programs](https://docs.microsoft.com/en-us/windows/win32/services/service-programs)
- [Service Security and Access Rights](https://docs.microsoft.com/en-us/windows/win32/services/service-security-and-access-rights)
- [Service Control Manager](https://docs.microsoft.com/en-us/windows/win32/services/service-control-manager)
- [sc.exe Command](https://docs.microsoft.com/en-us/windows-server/administration/windows-commands/sc-create)
