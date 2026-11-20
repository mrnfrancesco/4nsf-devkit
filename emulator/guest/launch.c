/*
 * launch - bring the emulated unit to life after Windows has booted.
 *
 * On the real unit WatchdogPuppy.exe starts and supervises the VW software. Here this small
 * program does the starting (no supervision), plus two emulator chores:
 *
 *  1. hide the Explorer taskbar: it covers the bottom 26 px, where the VW apps draw buttons
 *  2. add the serial ports COM2: and COM3: (the Windows image only knows COM1:), take over
 *     COM1: (GPS) and add COM4:, the unit's copy of the GPS data (docs/apps/navit.md)
 *  3. start the deploy agent on COM2: (host side: emulator/deploy.py)
 *  4. start the VW processes with the command lines from the unit's Watchdog_*.ini;
 *     like the watchdog, wait for the brand app to finish before starting the menu
 *  5. turn console output off, as on the unit (HKLM\Drivers\Console\OutputTo = -1, in the unit's
 *     ROM registry): programs that print (Navit) would otherwise open text windows here
 *  6. tell the phone program the language. It starts with built-in ger-DEU texts and waits for
 *     navigon.exe to send the unit's language (AppCom message 2, setting 2). navigon.exe is not
 *     started here, so this program sends that message in its place (as navigon's port 27011).
 *
 * Progress goes to the kernel debug output (emulator/work/serial.log on the host).
 */
#include <winsock2.h>
#include <windows.h>

#define FLASH  L"\\My Flash Disk\\"
#define DEV    FLASH L"dev\\"
#define DRIVER L"pl011poll.dll"

/* Same locale as the menu's default (VwUserShell Settings: LanguageLocale) = a .lng file name. */
#define LANGUAGE "eng-GBR"
#define WIDE_(text) L##text
#define WIDE(text) WIDE_(text)

static void log_line(const wchar_t *format, ...)
{
    wchar_t line[300];
    va_list args;

    va_start(args, format);
    wvsprintfW(line, format, args);
    va_end(args);
    OutputDebugStringW(line);
}

static void set_string(HKEY key, const wchar_t *name, const wchar_t *value)
{
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (wcslen(value) + 1) * sizeof *value);
}

static void set_dword(HKEY key, const wchar_t *name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof value);
}

/*
 * Register one more UART of the QEMU board as COMn:, served by our polled driver.
 * `registers` is the kernel virtual address of that UART's register block. `mirror`: name of
 * a message queue that also gets each received line, or NULL.
 */
static void add_serial_port(DWORD n, DWORD registers, const wchar_t *mirror)
{
    wchar_t key[80];
    HKEY settings;
    HANDLE device;

    wsprintfW(key, L"Drivers\\BuiltIn\\PL011POLL%d", n);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL, 0, 0, NULL, &settings, NULL) != ERROR_SUCCESS) {
        log_line(L"LAUNCH: COM%d: cannot create registry key", n);
        return;
    }
    set_string(settings, L"Dll", DRIVER);
    set_string(settings, L"Prefix", L"COM");
    set_dword(settings, L"Index", n);
    set_dword(settings, L"IoBase", registers);
    if (mirror)
        set_string(settings, L"Mirror", mirror);
    RegCloseKey(settings);

    device = ActivateDeviceEx(key, NULL, 0, NULL);
    log_line(L"LAUNCH: COM%d: at %08x -> %s", n, registers, device ? L"ok" : L"FAILED");
}

/*
 * The unit's GPS driver (COM2: there) copies every NMEA sentence into the message queue TX_NMEA;
 * its LoopbackCOM.dll serves that queue as COM4:, so a second program can read the GPS while
 * navigon.exe holds COM2:. Here the GPS is COM1: (host TCP 5555): our driver takes it over from
 * the image's own one to make the copy, and serves COM4: from the queue too, with the unit's
 * registry values (HKLM\Drivers\BuiltIn\GPS_DEBUG_COM) apart from the DLL name.
 */
static void add_gps_ports(DWORD com1_registers)
{
    DEVMGR_DEVICE_INFORMATION com1 = { sizeof com1 };
    HANDLE search = FindFirstDevice(DeviceSearchByLegacyName, L"COM1:", &com1);
    const wchar_t *key = L"Drivers\\BuiltIn\\GPS_DEBUG_COM";
    HKEY settings;
    HANDLE device;

    if (search != INVALID_HANDLE_VALUE) {
        FindClose(search);
        DeactivateDevice(com1.hDevice);
    }
    add_serial_port(1, com1_registers, L"TX_NMEA");
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL, 0, 0, NULL, &settings, NULL) != ERROR_SUCCESS)
        return;
    set_string(settings, L"Dll", DRIVER);
    set_string(settings, L"Prefix", L"COM");
    set_dword(settings, L"Index", 4);
    set_dword(settings, L"DeviceArrayIndex", 1);
    set_dword(settings, L"MaxMessageBytes", 0x5ee);
    set_string(settings, L"RXQUEUE", L"TX_NMEA");
    set_string(settings, L"TXQUEUE", L"RX_NMEA");
    RegCloseKey(settings);
    device = ActivateDeviceEx(key, NULL, 0, NULL);
    log_line(L"LAUNCH: COM4: GPS copy -> %s", device ? L"ok" : L"FAILED");
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

    add_serial_port(2, com1_registers + 0x10000, NULL);
    add_serial_port(3, com1_registers + 0x20000, NULL);
    add_gps_ports(com1_registers);
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

static BYTE *put32(BYTE *at, DWORD value)
{
    memcpy(at, &value, 4);
    return at + 4;
}

/*
 * Send AppCom message 2 [2, LANGUAGE] ("setting 2 = language changed") to the phone, port 27015.
 * Wire format: docs/apps/appcom.md §3-4; same bytes as
 *   tools/appcom.py encode 27011 2 '[2, "eng-GBR"]'
 */
static void send_language(void)
{
    BYTE datagram[64], *at = datagram;
    DWORD length = sizeof LANGUAGE - 1;
    struct sockaddr_in address = { 0 };
    WSADATA winsock;
    SOCKET udp;
    int sent = -1;

    at = put32(at, 0);                          /* header: bytes still to come in later datagrams */
    at = put32(at, 27011);                      /* header: sender port (navigon) */
    at = put32(at, 4); at = put32(at, 2);       /* List of 2 */
    at = put32(at, 8); at = put32(at, 2);       /*   Int 2 = message id */
    at = put32(at, 4); at = put32(at, 2);       /*   List of 2 */
    at = put32(at, 8); at = put32(at, 2);       /*     Int 2 = setting "language" */
    at = put32(at, 6); at = put32(at, length);  /*     Binary = locale, UTF-8, padded to 4 */
    memset(at, 0, (length + 3) & ~3);
    memcpy(at, LANGUAGE, length);
    at += (length + 3) & ~3;

    address.sin_family = AF_INET;
    address.sin_port = htons(27015);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    WSAStartup(MAKEWORD(2, 2), &winsock);
    udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp != INVALID_SOCKET) {
        sent = sendto(udp, (char *)datagram, at - datagram, 0, (struct sockaddr *)&address, sizeof address);
        closesocket(udp);
    }
    WSACleanup();
    log_line(L"LAUNCH: phone language " WIDE(LANGUAGE) L" -> %s", sent == at - datagram ? L"sent" : L"FAILED");
}

/* Waits up to `timeout_ms` for a top-level window of that class and title. */
static BOOL wait_window(const wchar_t *class_name, const wchar_t *title, DWORD timeout_ms)
{
    DWORD start = GetTickCount();

    while (!FindWindowW(class_name, title)) {
        if (GetTickCount() - start > timeout_ms)
            return FALSE;
        Sleep(500);
    }
    return TRUE;
}

static void no_console_windows(void)
{
    HKEY console;

    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Drivers\\Console", 0, NULL, 0, 0, NULL, &console, NULL) != ERROR_SUCCESS) {
        log_line(L"LAUNCH: cannot turn console output off");
        return;
    }
    set_dword(console, L"OutputTo", 0xFFFFFFFF);
    RegCloseKey(console);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show)
{
    HWND taskbar = FindWindowW(L"HHTaskBar", NULL);

    if (taskbar)
        ShowWindow(taskbar, SW_HIDE);

    add_serial_ports();
    no_console_windows();
    start(DEV L"ceagent.exe", L"COM2:", 0);
    start(FLASH L"VwUserShell\\VwCommunicationExe.exe", L"", 0);
    /* On a paired unit the brand app installs the boot pictures and exits within seconds.
     * (Unpaired, it would wait for a car for ever; the watchdog then never starts the menu.) */
    start(FLASH L"VwBrandDevice\\VwBrandDevice.exe", L"", 60000);
    start(FLASH L"VwUserShell\\VwUserShell.exe", L"-ShowWindow=1", 0);
    start(FLASH L"BT\\PhoneApplication.exe", L"-TSW_HIDE", 0);
    if (wait_window(L"PhoneApplication", L"PhoneApplication", 60000)) {
        Sleep(3000);    /* window first, AppCom listener a moment later */
        send_language();
    } else
        log_line(L"LAUNCH: no phone window after 60 s, language not sent");
    log_line(L"LAUNCH: done");
    return 0;
}
