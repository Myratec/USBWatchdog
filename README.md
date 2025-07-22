
# USBWatchdog

![Made with C++](https://img.shields.io/badge/Made%20with-C%2B%2B-00599C?style=for-the-badge) ![Platform](https://img.shields.io/badge/Platform-Windows-blue?style=for-the-badge&logo=windows&logoColor=white) ![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue?style=for-the-badge)



## Description

USBWatchdog (former WatchdogR) is a C++ Windows service that monitors newly connected USB devices. Only devices with a serial number present in a hardcoded whitelist are allowed; unauthorized devices trigger an automated Blue Screen of Death (BSOD). All relevant events are logged to the Windows Application Event Log.

### Features

-   **WMI Monitoring**: Subscribes to WMI events for `Win32_USBControllerDevice` insertions.
    
-   **Whitelist Enforcement**: Compares device serial numbers against an allowed list.
    
-   **Automatic BSOD**: Initiates a critical system error via undocumented NT APIs on unauthorized devices.
    
-   **Event Logging**: Writes Information, Warning, and Error entries to the Windows Application Event Log.
    
-   **Windows Service**: Runs as a native service and starts automatically at boot.
    

## Installation

1.  **Clone the repository**

    
2.  **Prerequisites**
    
    -   Windows 10 or later
        
    -   Visual Studio 2019 or newer with C++ workload
        
3.  **Build**
    
    -   Open `USBWatchdog.sln` in Visual Studio.
        
    -   Select **Release** and the appropriate platform (**x64** or **x86**).
        
    -   Build the solution (**Build → Rebuild Solution**).
        
4.  **Install the service**
    
    Open an elevated PowerShell or Command Prompt and run:
    
    ```
    # Stop and remove any existing service
    sc.exe stop "USBWatchdog"
    sc.exe delete "USBWatchdog"
    
    # Create the service (adjust path to the compiled EXE)
    # Choose a sneaky name
    sc.exe create "USBWatchdog" `
      binPath= C:\Path\to\USBWatchdog.exe\ `
      DisplayName= "USB Watchdog Service" `
      start= auto
    
    # Start the service
    sc.exe start "USBWatchdog"
    ```
    
5.  **Verify logs**
    
    -   Open **Event Viewer** (`eventvwr.msc`).
        
    -   Navigate to **Windows Logs → Application**.
        
    -   Filter by **Source = USBWatchdog** to see service entries.
        

## Configuration

The whitelist is currently hardcoded in `ServiceWorkerThread` for security reasons:

```
std::set<std::wstring> allowed = { L"1234567890ABCDEF" };
```

## License

This project is licensed under the **GPL v3**. See LICENSE for details.

----------

*© 2025 *
