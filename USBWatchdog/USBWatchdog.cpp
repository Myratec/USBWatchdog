#include <Windows.h>
#include <tchar.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <string>
#include <set>
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "advapi32.lib")

// Define NT status codes if not provided
#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022L)
#endif

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;

static TCHAR SERVICE_NAME[] = _T("WatcppR");

// Forward declarations
VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
VOID WINAPI ServiceCtrlHandler(DWORD);
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam);

// Helper function declarations
bool InitializeWMI(IWbemServices*& pSvc, IEnumWbemClassObject*& pEnum);
std::wstring ExtractSerialFromEvent(IWbemClassObject* pEvent);
void LogEvent(WORD type, const std::wstring& message);
bool TriggerBSOD();
void ForceShutdown();

int _tmain(int argc, TCHAR* argv[])
{
    SERVICE_TABLE_ENTRY ServiceTable[] =
    {
        {SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
        {NULL, NULL}
    };

    return StartServiceCtrlDispatcher(ServiceTable) ? 0 : GetLastError();
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle)
        return;

    // Initialize service status
    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 1;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // Create stop event
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent)
    {
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        g_ServiceStatus.dwCheckPoint = 1;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    // Service is running
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // Launch worker thread
    HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
    OutputDebugString(_T("My Sample Service: ServiceMain: Waiting for Worker Thread to complete"));

    // Wait until worker thread exits signaling service stop
    WaitForSingleObject(hThread, INFINITE);

    // Cleanup
    CloseHandle(g_ServiceStopEvent);

    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 3;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
    if (CtrlCode == SERVICE_CONTROL_STOP && g_ServiceStatus.dwCurrentState == SERVICE_RUNNING)
    {
        g_ServiceStatus.dwControlsAccepted = 0;
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        g_ServiceStatus.dwWin32ExitCode = 0;
        g_ServiceStatus.dwCheckPoint = 4;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

        // Signal the worker thread to stop
        SetEvent(g_ServiceStopEvent);
    }
}

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam)
{
    IWbemServices* pSvc = nullptr;
    IEnumWbemClassObject* pEnum = nullptr;

    // Initialize WMI
    if (!InitializeWMI(pSvc, pEnum))
    {
        LogEvent(EVENTLOG_ERROR_TYPE, L"WMI initialization failed.");
        return ERROR_SERVICE_SPECIFIC_ERROR;
    }

    // Hardcoded whitelist
    std::set<std::wstring> allowed = { L"200625851111C0904EFE" };

    while (WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0)
    {
        IWbemClassObject* pEvent = nullptr;
        ULONG returned = 0;
        HRESULT hr = pEnum->Next(2000, 1, &pEvent, &returned);
        if (SUCCEEDED(hr) && returned == 1)
        {
            std::wstring serial = ExtractSerialFromEvent(pEvent);
            LogEvent(EVENTLOG_INFORMATION_TYPE, L"USB angeschlossen. Seriennummer: " + serial);

            if (allowed.find(serial) == allowed.end())
            {
                LogEvent(EVENTLOG_WARNING_TYPE, L"Unerlaubtes USB-Gerät erkannt: " + serial);
                if (!TriggerBSOD())
                {
                    LogEvent(EVENTLOG_ERROR_TYPE, L"BSOD fehlerhaft. Führe Shutdown durch.");
                    ForceShutdown();
                }
            }
            pEvent->Release();
        }
    }

    // Cleanup WMI
    if (pEnum) pEnum->Release();
    if (pSvc) pSvc->Release();
    CoUninitialize();
    return ERROR_SUCCESS;
}

bool InitializeWMI(IWbemServices*& pSvc, IEnumWbemClassObject*& pEnum)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return false;

    hr = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL);
    if (FAILED(hr)) return false;

    IWbemLocator* pLoc = nullptr;
    hr = CoCreateInstance(
        CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr)) return false;

    hr = pLoc->ConnectServer(
        _bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    pLoc->Release();
    if (FAILED(hr)) return false;

    hr = CoSetProxyBlanket(
        pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE);
    if (FAILED(hr)) return false;

    // Query USB insert events
    BSTR query = SysAllocString(
        L"SELECT * FROM __InstanceCreationEvent WITHIN 2 "
        L"WHERE TargetInstance ISA 'Win32_USBControllerDevice'"
    );
    BSTR lang = SysAllocString(L"WQL");
    hr = pSvc->ExecNotificationQuery(
        lang, query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnum);
    SysFreeString(query);
    SysFreeString(lang);
    return SUCCEEDED(hr);
}

std::wstring ExtractSerialFromEvent(IWbemClassObject* pEvent)
{
    IWbemClassObject* pTarget = nullptr;
    VARIANT var;
    VariantInit(&var);
    pEvent->Get(L"TargetInstance", 0, &var, NULL, NULL);
    if (var.vt == VT_UNKNOWN)
    {
        IUnknown* pu = var.punkVal;
        pu->QueryInterface(IID_IWbemClassObject, (void**)&pTarget);
    }
    VariantClear(&var);

    std::wstring serial;
    if (pTarget)
    {
        VARIANT vDep;
        VariantInit(&vDep);
        pTarget->Get(L"Dependent", 0, &vDep, NULL, NULL);
        if (vDep.vt == VT_BSTR)
        {
            std::wstring path = vDep.bstrVal;
            auto pos = path.find(L"DeviceID=");
            if (pos != std::wstring::npos)
            {
                auto start = path.find(L'\"', pos) + 1;
                auto end = path.find(L'\"', start);
                std::wstring dev = path.substr(start, end - start);
                auto bs = dev.rfind(L"\\");
                serial = (bs != std::wstring::npos) ? dev.substr(bs + 1) : dev;
                auto amp = serial.find(L'&');
                if (amp != std::wstring::npos) serial.resize(amp);
            }
        }
        VariantClear(&vDep);
        pTarget->Release();
    }
    return serial;
}

void LogEvent(WORD type, const std::wstring& message)
{
    HANDLE hLog = RegisterEventSource(NULL, SERVICE_NAME);
    if (hLog)
    {
        LPCWSTR msg = message.c_str();
        ReportEvent(hLog, type, 0, 0, NULL, 1, 0, &msg, NULL);
        DeregisterEventSource(hLog);
    }
}

// NT functions
EXTERN_C NTSTATUS NTAPI RtlAdjustPrivilege(
    ULONG Privilege, BOOLEAN Enable, BOOLEAN CurrentThread, PBOOLEAN Enabled);
EXTERN_C NTSTATUS NTAPI NtRaiseHardError(
    NTSTATUS ErrorStatus, ULONG ParamCount, ULONG UnicodeMask,
    PULONG_PTR Params, ULONG ResponseOption, PULONG Response);

bool TriggerBSOD()
{
    BOOLEAN prev;
    NTSTATUS st = RtlAdjustPrivilege(19, TRUE, FALSE, &prev);
    if (st < 0) return false;
    ULONG resp = 0;
    st = NtRaiseHardError(STATUS_ACCESS_DENIED, 0, 0, NULL, 6, &resp);
    return (st >= 0);
}

void ForceShutdown()
{
    InitiateSystemShutdownEx(NULL, NULL, 0, TRUE, TRUE,
        SHTDN_REASON_MAJOR_HARDWARE | SHTDN_REASON_MINOR_SECURITY);
}
