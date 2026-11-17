/*
 * appcom - example and tool: listen to the messages the unit's programs send each other.
 *
 * AppCom messages are UDP datagrams on 127.0.0.1, one port per program (docs/apps/appcom.md).
 * The ports are only reachable from inside the unit, so this app is the bridge between them
 * and tools/appcom.py on your computer, which decodes and builds the datagrams:
 *
 *   appcom.exe                   listen on this app's own port (27112, from NgAppInfo.xml next
 *                                to this file) and append every datagram to capture.txt in the
 *                                app's folder, one line each:  TICK PORT LENGTH HEX
 *   appcom.exe 27018 27019       the same, also on the ports of programs that are not running
 *   appcom.exe send PORT HEX...  send datagrams (hex, as printed by tools/appcom.py encode) to
 *                                the program on PORT, then exit
 *   appcom.exe quit              stop the listening copy
 *
 * NgAppInfo.xml puts the app into the message groups of the unit's programs, so what they
 * send to a group arrives here too. They read that file when they start: deploy, then
 * restart the emulator. A tap hides the window (the app keeps listening); the entry under
 * "more" brings it back. Typical session, from the repository root:
 *
 *   make deploy APP=appcom
 *   .venv/bin/python emulator/deploy.py get '\My Flash Disk\Apps\appcom\capture.txt' capture.txt
 *   .venv/bin/python tools/appcom.py decode capture.txt
 *   .venv/bin/python emulator/deploy.py run '\My Flash Disk\Apps\appcom\appcom.exe' send 27017 \
 *       $(.venv/bin/python tools/appcom.py encode 27112 2 '[2, "ger-DEU"]')        # menu in German
 *
 * No AppCom DLL is needed: plain sockets (link with -lws2).
 */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define CLASS_NAME L"AppCom4NSF"
#define TITLE      L"AppCom4NSF"
#define OWN_PORT   27112            /* <Port> in NgAppInfo.xml */
#define LARGEST    4008             /* a datagram: 8 bytes of header, up to 4000 of the message */

static HWND main_window;
static wchar_t capture[MAX_PATH];   /* path of the capture file */
static CRITICAL_SECTION capture_lock;
static LONG received;               /* datagrams so far */
static wchar_t last[61];            /* start of the newest line, for the window */

static struct sockaddr_in local_address(int port)
{
    struct sockaddr_in address;

    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons((u_short)port);
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    return address;
}

/* capture.txt in the folder this program was started from */
static void name_capture(void)
{
    wchar_t *name;

    GetModuleFileNameW(NULL, capture, MAX_PATH - 12);
    name = wcsrchr(capture, L'\\');
    wcscpy(name ? name + 1 : capture, L"capture.txt");
}

/* Add a line to the capture. The file is closed again at once, so it can be fetched any time. */
static void append(const char *line, int length)
{
    HANDLE file;
    DWORD written;

    EnterCriticalSection(&capture_lock);
    file = CreateFileW(capture, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    SetFilePointer(file, 0, NULL, FILE_END);
    WriteFile(file, line, length, &written, NULL);
    WriteFile(file, "\r\n", 2, &written, NULL);
    CloseHandle(file);
    MultiByteToWideChar(CP_ACP, 0, line, length < 60 ? length : 60, last, 60);
    last[length < 60 ? length : 60] = 0;
    LeaveCriticalSection(&capture_lock);
    InvalidateRect(main_window, NULL, TRUE);
}

/* One of these threads per port: wait for datagrams, write each down as one capture line,
 * "TICK PORT LENGTH HEX": the format tools/appcom.py decode reads. */
static DWORD WINAPI listen_on(LPVOID parameter)
{
    static const char digits[] = "0123456789abcdef";
    int port = (int)parameter, length, used, i;
    struct sockaddr_in address = local_address(port);
    SOCKET udp = socket(AF_INET, SOCK_DGRAM, 0);
    BYTE *datagram = LocalAlloc(LMEM_FIXED, LARGEST);
    char *line = LocalAlloc(LMEM_FIXED, 40 + 2 * LARGEST);

    if (!datagram || !line || bind(udp, (struct sockaddr *)&address, sizeof address)) {
        char note[60];

        append(note, sprintf(note, "port %d is taken: its program is running", port));
        return 1;
    }
    for (;;) {
        length = recvfrom(udp, (char *)datagram, LARGEST, 0, NULL, NULL);
        if (length <= 0)
            continue;
        used = sprintf(line, "%lu %d %d ", GetTickCount(), port, length);
        for (i = 0; i < length; i++) {
            line[used++] = digits[datagram[i] >> 4];
            line[used++] = digits[datagram[i] & 15];
        }
        InterlockedIncrement(&received);
        append(line, used);
    }
}

static int hex_digit(wchar_t c)
{
    return c >= L'0' && c <= L'9' ? c - L'0' : (c | 0x20) >= L'a' && (c | 0x20) <= L'f' ? (c | 0x20) - L'a' + 10 : -1;
}

/* appcom.exe send PORT HEX HEX ...: returns the number of datagrams sent */
static int send_datagrams(wchar_t *text)
{
    struct sockaddr_in address = local_address(wcstol(text, NULL, 10));
    SOCKET udp = socket(AF_INET, SOCK_DGRAM, 0);
    BYTE *datagram = LocalAlloc(LMEM_FIXED, LARGEST);
    int sent = 0, length;

    while (datagram && (text = wcschr(text, L' ')) != NULL) {
        while (*text == L' ')
            text++;
        for (length = 0; length < LARGEST && hex_digit(text[0]) >= 0 && hex_digit(text[1]) >= 0; length++, text += 2)
            datagram[length] = hex_digit(text[0]) << 4 | hex_digit(text[1]);
        if (length && sendto(udp, (char *)datagram, length, 0, (struct sockaddr *)&address, sizeof address) == length)
            sent++;
    }
    closesocket(udp);
    return sent;
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint;
        RECT top, bottom;
        wchar_t text[120];
        HDC screen = BeginPaint(window, &paint);

        GetClientRect(window, &top);
        bottom = top;
        top.bottom = bottom.top = top.bottom / 2;
        wsprintfW(text, L"appcom - %d datagrams in capture.txt - tap to hide", received);
        DrawTextW(screen, text, -1, &top, DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
        DrawTextW(screen, last, -1, &bottom, DT_CENTER | DT_TOP | DT_SINGLELINE);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONUP:              /* keep listening; the menu shows the window again */
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show)
{
    WSADATA winsock;
    WNDCLASSW window_class;
    MSG message;
    wchar_t *argument;

    WSAStartup(MAKEWORD(2, 2), &winsock);
    if (!wcsncmp(command_line, L"send ", 5))
        return send_datagrams(command_line + 5) ? 0 : 1;
    if (!wcscmp(command_line, L"quit")) {
        HWND listener = FindWindowW(CLASS_NAME, TITLE);

        return listener && PostMessageW(listener, WM_CLOSE, 0, 0) ? 0 : 1;
    }

    memset(&window_class, 0, sizeof window_class);
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    window_class.lpszClassName = CLASS_NAME;
    RegisterClassW(&window_class);
    main_window = CreateWindowExW(WS_EX_TOPMOST, CLASS_NAME, TITLE, WS_POPUP | WS_VISIBLE,
                                  0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                                  NULL, NULL, instance, NULL);

    /* Listen on the own port and on every number given. Other words, such as the
     * " language=eng-GBR skin=VW" the menu passes, are not port numbers and are skipped. */
    InitializeCriticalSection(&capture_lock);
    name_capture();
    CreateThread(NULL, 0, listen_on, (LPVOID)OWN_PORT, 0, NULL);
    for (argument = command_line; argument && *argument; argument = wcschr(argument + 1, L' '))
        if (wcstol(argument, NULL, 10) > 0 && wcstol(argument, NULL, 10) != OWN_PORT)
            CreateThread(NULL, 0, listen_on, (LPVOID)wcstol(argument, NULL, 10), 0, NULL);

    while (GetMessageW(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
