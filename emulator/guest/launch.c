/*
 * launch - bring the emulated unit to life after Windows has booted.
 *
 * On the real unit WatchdogPuppy.exe starts and supervises the VW software. Here this small
 * program does the starting (no supervision), plus two emulator chores:
 *
 *  1. hide the Explorer taskbar: it covers the bottom 26 px, where the VW apps draw buttons
 *  2. add the serial ports COM2: and COM3: (the Windows image only knows COM1:)
 *  3. start the deploy agent on COM2: (host side: emulator/deploy.py)
 *  4. start the three VW processes with the command lines from the unit's Watchdog_*.ini;
 *     like the watchdog, wait for the brand app to finish before starting the menu
 *
 * Progress goes to the kernel debug output (emulator/work/serial.log on the host).
 */
#include <windows.h>

#define FLASH  L"\\My Flash Disk\\"
#define DEV    FLASH L"dev\\"
#define DRIVER L"pl011poll.dll"

static void log_line(const wchar_t *format, ...)
{
    wchar_t line[300];
    va_list args;

    va_start(args, format);
    wvsprintfW(line, format, args);
    va_end(args);
    OutputDebugStringW(line);
}

/*
 * Register one more UART of the QEMU board as COMn:, served by our polled driver.
 * `registers` is the kernel virtual address of that UART's register block.
 */
static void add_serial_port(DWORD n, DWORD registers)
{
    wchar_t key[80];
    HKEY settings;
    HANDLE device;

    wsprintfW(key, L"Drivers\\BuiltIn\\PL011POLL%d", n);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL, 0, 0, NULL, &settings, NULL) != ERROR_SUCCESS) {
        log_line(L"LAUNCH: COM%d: cannot create registry key", n);
        return;
    }
    RegSetValueExW(settings, L"Dll", 0, REG_SZ, (const BYTE *)DRIVER, sizeof DRIVER);
    RegSetValueExW(settings, L"Prefix", 0, REG_SZ, (const BYTE *)L"COM", sizeof L"COM");
    RegSetValueExW(settings, L"Index", 0, REG_DWORD, (const BYTE *)&n, sizeof n);
    RegSetValueExW(settings, L"IoBase", 0, REG_DWORD, (const BYTE *)&registers, sizeof registers);
    RegCloseKey(settings);

    device = ActivateDeviceEx(key, NULL, 0, NULL);
    log_line(L"LAUNCH: COM%d: at %08x -> %s", n, registers, device ? L"ok" : L"FAILED");
}

static void add_serial_ports(void)
{
    HKEY com1;
    DWORD com1_registers = 0, size = sizeof com1_registers;

    /* Where COM1: lives tells us where the others are: the board's UARTs are 0x10000 apart. */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Drivers\\BuiltIn\\pl011", 0, 0, &com1) == ERROR_SUCCESS) {
        RegQueryValueExW(com1, L"IoBase", NULL, NULL, (BYTE *)&com1_registers, &size);
        RegCloseKey(com1);
    }
    if (!com1_registers) {
        log_line(L"LAUNCH: COM1: not found in the registry, no extra serial ports");
        return;
    }
    /* Drivers are loaded by name from \Windows (a RAM disk, so this is redone at every boot). */
    if (!CopyFileW(DEV DRIVER, L"\\Windows\\" DRIVER, FALSE))
        log_line(L"LAUNCH: cannot copy " DRIVER L" to \\Windows (error %d)", GetLastError());

    add_serial_port(2, com1_registers + 0x10000);
    add_serial_port(3, com1_registers + 0x20000);
}

/* Starts a program. With `finish_ms`, waits that long for it to exit before going on. */
static void start(const wchar_t *program, const wchar_t *arguments, DWORD finish_ms)
{
    PROCESS_INFORMATION process;
    BOOL ok = CreateProcessW(program, (LPWSTR)arguments, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &process);

    log_line(L"LAUNCH: %s -> %s", program, ok ? L"started" : L"FAILED");
    if (ok) {
        if (finish_ms && WaitForSingleObject(process.hProcess, finish_ms) != WAIT_OBJECT_0)
            log_line(L"LAUNCH: %s still runs after %d s, going on", program, finish_ms / 1000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    Sleep(1500);        /* the VW processes expect each other to be up, in this order */
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show)
{
    HWND taskbar = FindWindowW(L"HHTaskBar", NULL);

    if (taskbar)
        ShowWindow(taskbar, SW_HIDE);

    add_serial_ports();
    start(DEV L"ceagent.exe", L"COM2:", 0);
    start(FLASH L"VwUserShell\\VwCommunicationExe.exe", L"", 0);
    /* On a paired unit the brand app installs the boot pictures and exits within seconds.
     * (Unpaired, it would wait for a car for ever; the watchdog then never starts the menu.) */
    start(FLASH L"VwBrandDevice\\VwBrandDevice.exe", L"", 60000);
    start(FLASH L"VwUserShell\\VwUserShell.exe", L"-ShowWindow=1", 0);
    log_line(L"LAUNCH: done");
    return 0;
}
