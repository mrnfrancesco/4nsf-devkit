/*
 * navit - launcher for Navit (open-source navigation, OpenStreetMap maps) from the VW menu.
 *
 * Navit itself lives in \My Flash Disk\Navit\ (apps/navit/fetch.sh prepares it). This program
 * is what the menu starts: it lists the map files (*.bin) found in
 *     \My Flash Disk\Navit\maps\   and   \Storage Card\Navit\maps\
 * lets the user tick the ones to load, writes them to \My Flash Disk\Navit\maps.xml (which
 * Navit's navit.xml includes), starts Navit and waits until it exits. The ticks are read back
 * from maps.xml the next time, so the last choice is the default.
 *
 * The menu finds a running Navit by its window (class NAVGRA, title "Navit": NgAppInfo.xml) and
 * brings it to the front instead of starting this program again.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define CLASS_NAME   L"NavitLauncher4NSF"
#define TITLE        L"Navit maps"
#define NAVIT_DIR    L"\\My Flash Disk\\Navit"
#define NAVIT_EXE    NAVIT_DIR L"\\navit.exe"
#define MAPS_XML     NAVIT_DIR L"\\maps.xml"

static const wchar_t *const map_folders[] = {
    NAVIT_DIR L"\\maps",
    L"\\Storage Card\\Navit\\maps",
};

#define MAX_MAPS  64
#define HEADER    32          /* layout for the 480x272 screen: header, 5 rows, button bar */
#define ROW       40
#define ROWS      5
#define FOOTER    40

struct map {
    wchar_t path[MAX_PATH];
    wchar_t name[MAX_PATH];
    DWORD megabytes;
    BOOL selected;
};

static struct map maps[MAX_MAPS];
static int map_count, first_row;
static const wchar_t *message = L"";
static HFONT font;

/* ---- maps.xml ------------------------------------------------------------------------- */

/* Paths of the maps in the existing maps.xml (UTF-8, one data="..." per map) -> selected. */
static void read_selection(void)
{
    HANDLE file = CreateFileW(MAPS_XML, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    static char text[16384];
    DWORD size = 0;
    char *at;
    int i;

    if (file == INVALID_HANDLE_VALUE) {             /* first start: everything ticked */
        for (i = 0; i < map_count; i++)
            maps[i].selected = TRUE;
        return;
    }
    ReadFile(file, text, sizeof text - 1, &size, NULL);
    CloseHandle(file);
    text[size] = 0;
    for (at = strstr(text, "data=\""); at; at = strstr(at, "data=\"")) {
        char *end;
        wchar_t path[MAX_PATH];

        at += 6;
        end = strchr(at, '"');
        if (!end)
            break;
        *end = 0;
        MultiByteToWideChar(CP_UTF8, 0, at, -1, path, MAX_PATH);
        for (i = 0; i < map_count; i++)
            if (!_wcsicmp(path, maps[i].path))
                maps[i].selected = TRUE;
        at = end + 1;
    }
}

static void write_text(HANDLE file, const char *text)
{
    DWORD written;
    WriteFile(file, text, strlen(text), &written, NULL);
}

static BOOL write_selection(void)
{
    HANDLE file = CreateFileW(MAPS_XML, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    int i;

    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    write_text(file, "<?xml version=\"1.0\"?>\n"
                     "<!-- Written by the launcher (\\My Flash Disk\\Apps\\navit\\navit.exe) on every start. -->\n"
                     "<mapset enabled=\"yes\">\n");
    for (i = 0; i < map_count; i++) {
        char path[MAX_PATH * 3], name[MAX_PATH * 3], line[sizeof path + sizeof name + 80];

        if (!maps[i].selected)
            continue;
        /* file names cannot contain '"', '<' or '>'; '&' is the one character to escape */
        if (wcschr(maps[i].path, L'&'))
            continue;
        WideCharToMultiByte(CP_UTF8, 0, maps[i].path, -1, path, sizeof path, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, maps[i].name, -1, name, sizeof name, NULL, NULL);
        sprintf(line, "  <map type=\"binfile\" enabled=\"yes\" description=\"%s\" data=\"%s\"/>\n", name, path);
        write_text(file, line);
    }
    write_text(file, "</mapset>\n");
    CloseHandle(file);
    return TRUE;
}

/* ---- map files ------------------------------------------------------------------------- */

static void find_maps(void)
{
    int f;

    for (f = 0; f < (int)(sizeof map_folders / sizeof *map_folders); f++) {
        WIN32_FIND_DATAW found;
        wchar_t pattern[MAX_PATH];
        HANDLE search;

        wsprintfW(pattern, L"%s\\*.bin", map_folders[f]);
        search = FindFirstFileW(pattern, &found);
        if (search == INVALID_HANDLE_VALUE)
            continue;
        do {
            struct map *map = &maps[map_count];
            wchar_t *dot;

            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            wsprintfW(map->path, L"%s\\%s", map_folders[f], found.cFileName);
            wcscpy(map->name, found.cFileName);
            if ((dot = wcsrchr(map->name, L'.')) != NULL)
                *dot = 0;
            if (f > 0)
                wcscat(map->name, L" (card)");
            /* rounded to the nearest MB, at least 1 */
            map->megabytes = (found.nFileSizeHigh << 12) + (found.nFileSizeLow >> 20)
                             + ((found.nFileSizeLow >> 19) & 1);
            if (map->megabytes == 0)
                map->megabytes = 1;
            map->selected = FALSE;
            map_count++;
        } while (map_count < MAX_MAPS && FindNextFileW(search, &found));
        FindClose(search);
    }
}

/* ---- Navit ----------------------------------------------------------------------------- */

/*
 * A worker thread starts Navit and watches it, and tells the window by message: Navit's
 * window is there (WM_NAVIT_UP), Navit has exited (WM_NAVIT_DONE) or did not start
 * (WM_NAVIT_FAILED). The window thread never waits, so the picker keeps answering while Navit
 * loads (about 7 s of guest time; 2-3 minutes of real time in the emulator, which emulates the CPU).
 */
#define WM_NAVIT_UP     (WM_APP + 1)
#define WM_NAVIT_DONE   (WM_APP + 2)
#define WM_NAVIT_FAILED (WM_APP + 3)
static BOOL navit_starting;

static DWORD WINAPI navit_thread(LPVOID parameter)
{
    HWND window = parameter, navit = NULL;
    PROCESS_INFORMATION process;
    DWORD started = GetTickCount();
    wchar_t line[64];

    if (!CreateProcessW(NAVIT_EXE, NULL, NULL, NULL, FALSE, 0, NULL, NULL, NULL, &process)) {
        PostMessageW(window, WM_NAVIT_FAILED, 0, 0);
        return 0;
    }
    CloseHandle(process.hThread);
    while (WaitForSingleObject(process.hProcess, 250) == WAIT_TIMEOUT && !navit)
        navit = FindWindowW(L"NAVGRA", L"Navit");
    if (navit) {
        wsprintfW(line, L"NAVIT: window after %u ms", GetTickCount() - started);
        OutputDebugStringW(line);               /* emulator: emulator/work/serial.log */
        PostMessageW(window, WM_NAVIT_UP, 0, (LPARAM)navit);
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hProcess);
    PostMessageW(window, WM_NAVIT_DONE, 0, 0);
    return 0;
}

static BOOL start_navit(HWND window)
{
    HANDLE thread = CreateThread(NULL, 0, navit_thread, window, 0, NULL);

    if (!thread)
        return FALSE;
    CloseHandle(thread);
    navit_starting = TRUE;
    return TRUE;
}

static void navit_up(HWND window, HWND navit)
{
    /* Navit does not come to the front by itself: the VW menu would stay above it */
    SetForegroundWindow(navit);
    ShowWindow(window, SW_HIDE);
}

static void navit_done(HWND window)
{
    /* Navit shows the Windows CE taskbar when it exits; the unit has none, the emulator has */
    HWND taskbar = FindWindowW(L"HHTaskBar", NULL);

    if (taskbar)
        ShowWindow(taskbar, SW_HIDE);
    DestroyWindow(window);
}

/* ---- screen ---------------------------------------------------------------------------- */

enum { BUTTON_BACK, BUTTON_UP, BUTTON_DOWN, BUTTON_START, BUTTONS };
static const wchar_t *const button_text[BUTTONS] = { L"Back", L"<", L">", L"Start Navit" };
static const RECT button_rect[BUTTONS] = {
    { 0, 272 - FOOTER, 110, 272 }, { 130, 272 - FOOTER, 200, 272 },
    { 210, 272 - FOOTER, 280, 272 }, { 300, 272 - FOOTER, 480, 272 },
};

static BOOL paging(void)
{
    return map_count > ROWS;
}

static void paint(HWND window)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(window, &ps);
    RECT r = { 0, 0, 480, 272 };
    HGDIOBJ old_font = SelectObject(dc, font);
    wchar_t text[MAX_PATH + 32];
    int i;

    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));

    r.left = 8; r.right = 472; r.bottom = HEADER;
    if (*message) {                                 /* a problem replaces the title */
        SetTextColor(dc, RGB(255, 200, 0));
        wcscpy(text, message);
    } else if (paging())
        wsprintfW(text, L"Maps to load   %d-%d of %d", first_row + 1,
                  min(first_row + ROWS, map_count), map_count);
    else
        wcscpy(text, L"Maps to load");
    DrawTextW(dc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SetTextColor(dc, RGB(255, 255, 255));
    r.left = 0; r.right = 480; r.top = HEADER - 1; r.bottom = HEADER;
    FillRect(dc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));

    if (map_count == 0) {
        r.left = 8; r.right = 472; r.top = HEADER; r.bottom = 272 - FOOTER;
        DrawTextW(dc, L"No maps found.\nCopy Navit maps (*.bin) to\n\\My Flash Disk\\Navit\\maps\\ or\n"
                      L"\\Storage Card\\Navit\\maps\\", -1, &r, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    for (i = 0; i < ROWS && first_row + i < map_count; i++) {
        const struct map *map = &maps[first_row + i];
        RECT box = { 12, HEADER + i * ROW + 8, 36, HEADER + i * ROW + 32 };

        SelectObject(dc, GetStockObject(WHITE_PEN));
        SelectObject(dc, GetStockObject(BLACK_BRUSH));
        Rectangle(dc, box.left, box.top, box.right, box.bottom);
        if (map->selected) {                        /* check mark */
            POINT tick[3] = { { box.left + 4, box.top + 12 }, { box.left + 10, box.top + 19 },
                              { box.left + 20, box.top + 5 } };
            HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
            HGDIOBJ old_pen = SelectObject(dc, pen);
            Polyline(dc, tick, 3);
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        }
        r.left = 48; r.right = 472; r.top = HEADER + i * ROW; r.bottom = r.top + ROW;
        wsprintfW(text, L"%s   %u MB", map->name, (unsigned)map->megabytes);
        DrawTextW(dc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    for (i = 0; i < BUTTONS; i++) {
        RECT b = button_rect[i];
        if ((i == BUTTON_UP || i == BUTTON_DOWN) && !paging())
            continue;
        if (i == BUTTON_START && map_count == 0)
            continue;
        InflateRect(&b, -2, -2);
        SelectObject(dc, GetStockObject(WHITE_PEN));
        SelectObject(dc, GetStockObject(DKGRAY_BRUSH));
        RoundRect(dc, b.left, b.top, b.right, b.bottom, 10, 10);
        DrawTextW(dc, button_text[i], -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(dc, old_font);
    EndPaint(window, &ps);
}

static void tap(HWND window, int x, int y)
{
    POINT p = { x, y };
    int i, count = 0;

    if (navit_starting)             /* Navit is starting: nothing to tap */
        return;
    message = L"";
    if (y >= HEADER && y < HEADER + ROWS * ROW && first_row + (y - HEADER) / ROW < map_count) {
        struct map *map = &maps[first_row + (y - HEADER) / ROW];
        map->selected = !map->selected;
    }
    for (i = 0; i < BUTTONS; i++)
        if (PtInRect(&button_rect[i], p))
            break;
    switch (i) {
    case BUTTON_BACK:
        DestroyWindow(window);
        return;
    case BUTTON_UP:
        if (paging() && first_row > 0)
            first_row -= ROWS;
        break;
    case BUTTON_DOWN:
        if (paging() && first_row + ROWS < map_count)
            first_row += ROWS;
        break;
    case BUTTON_START:
        if (map_count == 0)
            break;
        for (i = 0; i < map_count; i++)
            count += maps[i].selected;
        if (count == 0) {
            message = L"Tick at least one map.";
            break;
        }
        if (!write_selection()) {
            message = L"Cannot write " MAPS_XML;
            break;
        }
        if (GetFileAttributesW(NAVIT_EXE) == 0xFFFFFFFF) {
            message = L"Navit is missing: " NAVIT_EXE;
            break;
        }
        message = L"Starting Navit...";             /* loading a map takes a while */
        InvalidateRect(window, NULL, FALSE);
        UpdateWindow(window);
        if (!start_navit(window))
            message = L"Navit did not start.";
        break;
    }
    InvalidateRect(window, NULL, FALSE);
}

static LRESULT CALLBACK window_procedure(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_PAINT:
        paint(window);
        return 0;
    case WM_NAVIT_UP:
        navit_up(window, (HWND)lparam);
        return 0;
    case WM_NAVIT_DONE:
        navit_done(window);
        return 0;
    case WM_NAVIT_FAILED:
        navit_starting = FALSE;
        message = L"Navit did not start.";
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:              /* the touch screen sends mouse messages */
        tap(window, (short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, msg, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show)
{
    WNDCLASSW window_class;
    LOGFONTW logfont;
    HWND running;
    MSG msg;

    /* already open (Navit or this picker): bring it to the front */
    running = FindWindowW(L"NAVGRA", L"Navit");
    if (!running)
        running = FindWindowW(CLASS_NAME, TITLE);
    if (running) {
        SetForegroundWindow(running);
        return 0;
    }

    find_maps();
    read_selection();

    memset(&logfont, 0, sizeof logfont);
    logfont.lfHeight = -20;
    logfont.lfWeight = FW_NORMAL;
    wcscpy(logfont.lfFaceName, L"Tahoma");
    font = CreateFontIndirectW(&logfont);

    memset(&window_class, 0, sizeof window_class);
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window_class.lpszClassName = CLASS_NAME;
    RegisterClassW(&window_class);
    CreateWindowExW(WS_EX_TOPMOST, CLASS_NAME, TITLE, WS_POPUP | WS_VISIBLE,
                    0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                    NULL, NULL, instance, NULL);

    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DeleteObject(font);
    return (int)msg.wParam;
}
