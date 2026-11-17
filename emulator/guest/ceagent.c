/*
 * ceagent - deploy agent: lets the host copy files in and out of the emulator and start
 * programs, over a serial port. Host side: emulator/deploy.py.
 *
 *   ceagent.exe COM2:      (launch.exe starts it like this; COM2: is TCP 5556 on the host)
 *
 * Protocol, little endian, same frame in both directions:
 *
 *   u32 magic "CEA1" | u8 command (request) or status (reply) | u32 length | payload
 *
 * Reply status 0 = OK, otherwise a Win32 error code. Strings are UTF-16, NUL terminated.
 * The commands and their payloads are listed in the enum below.
 */
#include <windows.h>
#include <tlhelp32.h>

#define MAGIC       0x31414543u     /* "CEA1" */
#define MAX_PAYLOAD (64 * 1024)
#define HEADER_SIZE 9
#define STATUS_OK   0

enum command {
    CMD_PING  = 1,  /* -                      -> "ceagent 1"                                  */
    CMD_PUT   = 2,  /* path, u32 size         -> OK; the host then sends CHUNKs and a CLOSE   */
    CMD_CHUNK = 3,  /* file data              -> OK                                           */
    CMD_CLOSE = 4,  /* -                      -> OK                                           */
    CMD_GET   = 5,  /* path                   -> u32 size; the agent then sends CHUNKs, CLOSE */
    CMD_RUN   = 6,  /* u32 wait_ms, cmdline   -> u32 pid, u32 exit code (0xFFFFFFFF: running) */
    CMD_LIST  = 7,  /* folder                 -> text lines "name\tsize\tattributes\n"        */
    CMD_DEL   = 8,  /* path                   -> OK                                           */
    CMD_MKDIR = 9,  /* path                   -> OK                                           */
    CMD_EXIT  = 10, /* -                      -> OK, then the agent exits                     */
    CMD_PS    = 11  /* -                      -> text lines "program\tprocess id\n"            */
};

static HANDLE port = INVALID_HANDLE_VALUE;
static HANDLE upload = INVALID_HANDLE_VALUE;    /* file being written by PUT/CHUNK/CLOSE */

static void log_line(const wchar_t *format, ...)
{
    wchar_t line[256];
    va_list args;

    va_start(args, format);
    _vsnwprintf(line, 255, format, args);       /* cut what does not fit: a command line can be long */
    va_end(args);
    line[255] = 0;
    OutputDebugStringW(line);
}

/* ---- serial port ------------------------------------------------------------------------ */

static BOOL open_port(const wchar_t *name)
{
    DCB settings;
    COMMTIMEOUTS timeouts;

    port = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (port == INVALID_HANDLE_VALUE) {
        log_line(L"CEAGENT: cannot open %s (error %d)", name, GetLastError());
        return FALSE;
    }
    memset(&settings, 0, sizeof settings);
    settings.DCBlength = sizeof settings;
    GetCommState(port, &settings);
    settings.BaudRate = 115200;
    settings.ByteSize = 8;
    settings.Parity = NOPARITY;
    settings.StopBits = ONESTOPBIT;
    settings.fBinary = TRUE;
    settings.fOutxCtsFlow = settings.fOutxDsrFlow = FALSE;
    settings.fOutX = settings.fInX = FALSE;
    settings.fDtrControl = DTR_CONTROL_ENABLE;
    settings.fRtsControl = RTS_CONTROL_ENABLE;
    SetCommState(port, &settings);

    memset(&timeouts, 0, sizeof timeouts);
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 500;
    timeouts.WriteTotalTimeoutConstant = 5000;
    SetCommTimeouts(port, &timeouts);
    SetupComm(port, 65536, 65536);
    PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return TRUE;
}

/* Blocks until exactly `length` bytes have arrived. */
static BOOL read_exactly(void *buffer, DWORD length)
{
    BYTE *p = buffer;

    while (length > 0) {
        DWORD n = 0;
        if (!ReadFile(port, p, length, &n, NULL))
            return FALSE;
        p += n;                 /* n == 0 is a read timeout: keep waiting */
        length -= n;
    }
    return TRUE;
}

static BOOL write_all(const void *buffer, DWORD length)
{
    const BYTE *p = buffer;

    while (length > 0) {
        DWORD n = 0;
        if (!WriteFile(port, p, length, &n, NULL) || n == 0)
            return FALSE;
        p += n;
        length -= n;
    }
    return TRUE;
}

/* ---- frames ----------------------------------------------------------------------------- */

static BOOL send_frame(BYTE status, const void *payload, DWORD length)
{
    BYTE header[HEADER_SIZE];
    DWORD magic = MAGIC;

    memcpy(header, &magic, 4);
    header[4] = status;
    memcpy(header + 5, &length, 4);
    return write_all(header, sizeof header) && (length == 0 || write_all(payload, length));
}

static BOOL reply_ok(void) { return send_frame(STATUS_OK, NULL, 0); }

static BOOL reply_error(DWORD win32_error)
{
    if (win32_error == 0 || win32_error > 254)      /* must fit the status byte and not read as OK */
        win32_error = 1;
    return send_frame((BYTE)win32_error, NULL, 0);
}

static BOOL reply(BOOL succeeded) { return succeeded ? reply_ok() : reply_error(GetLastError()); }

static BOOL receive_frame(BYTE *command, BYTE *payload, DWORD *length)
{
    BYTE header[HEADER_SIZE];
    DWORD magic;

    for (;;) {                                      /* skip stray bytes until the magic shows up */
        if (!read_exactly(header, 1))
            return FALSE;
        if (header[0] != (MAGIC & 0xff))
            continue;
        if (!read_exactly(header + 1, sizeof header - 1))
            return FALSE;
        memcpy(&magic, header, 4);
        if (magic == MAGIC)
            break;
    }
    *command = header[4];
    memcpy(length, header + 5, 4);
    if (*length > MAX_PAYLOAD)
        return FALSE;
    return *length == 0 || read_exactly(payload, *length);
}

/* ---- commands --------------------------------------------------------------------------- */

static void close_upload(void)
{
    if (upload != INVALID_HANDLE_VALUE)
        CloseHandle(upload);
    upload = INVALID_HANDLE_VALUE;
}

static void put(const wchar_t *path)
{
    close_upload();
    upload = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    log_line(L"CEAGENT: put %s%s", path, upload == INVALID_HANDLE_VALUE ? L" FAILED" : L"");
    reply(upload != INVALID_HANDLE_VALUE);
}

static void chunk(const BYTE *data, DWORD length)
{
    DWORD written;

    if (upload != INVALID_HANDLE_VALUE && WriteFile(upload, data, length, &written, NULL) && written == length)
        reply_ok();
    else
        reply_error(ERROR_WRITE_FAULT);
}

static void get(const wchar_t *path, BYTE *scratch)
{
    /* sharing: also a log file that a program still has open for writing */
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, n;

    if (file == INVALID_HANDLE_VALUE) {
        reply_error(GetLastError());
        return;
    }
    size = GetFileSize(file, NULL);
    send_frame(STATUS_OK, &size, sizeof size);
    while (ReadFile(file, scratch, 32768, &n, NULL) && n > 0)
        if (!send_frame(CMD_CHUNK, scratch, n))
            break;
    send_frame(CMD_CLOSE, NULL, 0);
    CloseHandle(file);
}

/* Split `"program" arguments` (quotes optional) in place; returns the arguments. */
static wchar_t *split_command_line(wchar_t *command_line, wchar_t **program)
{
    wchar_t stop = L' ', *p = command_line;

    if (*p == L'"') {
        stop = L'"';
        p++;
    }
    *program = p;
    while (*p && *p != stop)
        p++;
    if (*p)
        *p++ = 0;
    while (*p == L' ')
        p++;
    return p;
}

static void run(BYTE *payload, DWORD length)
{
    struct { DWORD pid, exit_code; } result;
    DWORD wait_ms;
    wchar_t *program, *arguments;
    PROCESS_INFORMATION process;

    if (length < 6) {
        reply_error(ERROR_INVALID_PARAMETER);
        return;
    }
    memcpy(&wait_ms, payload, 4);
    arguments = split_command_line((wchar_t *)(payload + 4), &program);
    log_line(L"CEAGENT: run '%s' '%s'", program, arguments);
    if (!CreateProcessW(program, *arguments ? arguments : NULL, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &process)) {
        reply_error(GetLastError());
        return;
    }
    result.pid = process.dwProcessId;
    result.exit_code = 0xFFFFFFFF;
    if (wait_ms && WaitForSingleObject(process.hProcess, wait_ms) == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &result.exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    send_frame(STATUS_OK, &result, sizeof result);
}

static void list(const wchar_t *folder, BYTE *scratch)
{
    wchar_t pattern[MAX_PATH + 4], *text = (wchar_t *)scratch;
    WIN32_FIND_DATAW entry;
    HANDLE search;
    int used = 0, capacity = MAX_PAYLOAD / 2 - MAX_PATH - 64;

    wsprintfW(pattern, L"%s\\*", folder);
    search = FindFirstFileW(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        reply_error(GetLastError());
        return;
    }
    do {
        used += wsprintfW(text + used, L"%s\t%u\t%x\n", entry.cFileName, entry.nFileSizeLow, entry.dwFileAttributes);
    } while (used < capacity && FindNextFileW(search, &entry));
    FindClose(search);
    send_frame(STATUS_OK, text, used * 2);
}

static void processes(BYTE *scratch)
{
    wchar_t *text = (wchar_t *)scratch;
    PROCESSENTRY32 entry;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    int used = 0, capacity = MAX_PAYLOAD / 2 - MAX_PATH - 16;

    if (snapshot == INVALID_HANDLE_VALUE) {
        reply_error(GetLastError());
        return;
    }
    entry.dwSize = sizeof entry;
    if (Process32First(snapshot, &entry))
        do {
            used += wsprintfW(text + used, L"%s\t%x\n", entry.szExeFile, entry.th32ProcessID);
        } while (used < capacity && Process32Next(snapshot, &entry));
    CloseToolhelp32Snapshot(snapshot);
    send_frame(STATUS_OK, text, used * 2);
}

/* ---- main loop -------------------------------------------------------------------------- */

/* Serves requests until the link fails (returns TRUE) or the host asks to exit (FALSE). */
static BOOL serve(BYTE *payload, BYTE *scratch)
{
    BYTE command;
    DWORD length;

    while (receive_frame(&command, payload, &length)) {
        payload[length] = payload[length + 1] = 0;      /* so string payloads are terminated */
        switch (command) {
        case CMD_PING:  send_frame(STATUS_OK, L"ceagent 1", sizeof L"ceagent 1"); break;
        case CMD_PUT:   put((wchar_t *)payload); break;
        case CMD_CHUNK: chunk(payload, length); break;
        case CMD_CLOSE: close_upload(); reply_ok(); break;
        case CMD_GET:   get((wchar_t *)payload, scratch); break;
        case CMD_RUN:   run(payload, length); break;
        case CMD_LIST:  list((wchar_t *)payload, scratch); break;
        case CMD_DEL:   reply(DeleteFileW((wchar_t *)payload)); break;
        case CMD_MKDIR: reply(CreateDirectoryW((wchar_t *)payload, NULL)); break;
        case CMD_PS:    processes(scratch); break;
        case CMD_EXIT:  reply_ok(); return FALSE;
        default:        reply_error(ERROR_INVALID_FUNCTION); break;
        }
    }
    return TRUE;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show)
{
    const wchar_t *name = (command_line && *command_line) ? command_line : L"COM2:";
    BYTE *payload = LocalAlloc(LMEM_FIXED, MAX_PAYLOAD + 2);
    BYTE *scratch = LocalAlloc(LMEM_FIXED, MAX_PAYLOAD);

    log_line(L"CEAGENT: starting on %s", name);
    if (!payload || !scratch || !open_port(name))
        return 1;
    while (serve(payload, scratch)) {                   /* link error: clear the line, carry on */
        PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR);
        Sleep(200);
    }
    return 0;
}
