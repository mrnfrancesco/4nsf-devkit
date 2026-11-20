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
 *
 * While Navit runs, this program also listens on its AppCom port for the menu's overlay state
 * (message 31, group Settings: door picture, parking sensors, phone call, ...). When an overlay
 * that came up over Navit ends, the menu shows its own main screen; this program puts Navit
 * back in front. Plain sockets, no AppCom DLL (docs/apps/appcom.md).
 *
 * "navit.exe menu" (the home button on Navit's map, navit.xml.diff) shows the VW menu and leaves
 * Navit running behind it; the menu's entry for Navit brings it back.
 *
 * Country: the unit's navigation sends the country of the current GPS position about once a
 * second (AppCom message 11011, group SpeedInfoGps). The launcher keeps the last one, as two
 * letters, in \My Flash Disk\Navit\data\country.txt (before any GPS: the country of the VW menu's
 * language); navitsnd.dll gives it to Navit as the default country of the town search.
 *
 * Voice: Navit's eSpeak speaks the VW menu's language (" language=ita-ITA" on the launcher's
 * command line); the launcher writes it into the <speech> line of Navit's navit.xml. Navit's menus
 * stay English: this Navit build ignores the language settings for them (docs/apps/navit.md).
 */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define CLASS_NAME   L"NavitLauncher4NSF"
#define TITLE        L"Navit maps"
#define NAVIT_DIR    L"\\My Flash Disk\\Navit"
#define NAVIT_EXE    NAVIT_DIR L"\\navit.exe"
#define MAPS_XML     NAVIT_DIR L"\\maps.xml"
#define APPCOM_PORT  27114        /* <Port> in NgAppInfo.xml */
#define NAVIT_XML    NAVIT_DIR L"\\navit.xml"
#define DATA_DIR     NAVIT_DIR L"\\data"           /* Navit's own data: last position, ... */
#define COUNTRY_TXT  DATA_DIR L"\\country.txt"     /* last country seen, 2 letters; read by navitsnd.dll */

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

/* ---- language and country ------------------------------------------------------------- */

static char vw_language[8];             /* "ita-ITA": the VW menu's language, from the command line */
static char country[3];                 /* "IT": the country saved in country.txt */

/* ISO 3166 alpha-3 + alpha-2, one after the other (from Navit's country.c) */
static const char iso3_iso2[] =
    "ANDADAREAEAFGAFATGAGAIAAIALBALARMAMANTANAGOAOATAAQARGARASMASAUTATAUSAUABWAWALAAXAZEAZBIHBABRBBBBGDBD"
    "BELBEBFABFBGRBGBHRBHBDIBIBENBJBLMBLBMUBMBRNBNBOLBOBESBQBRABRBHSBSBTNBTBVTBVBWABWBLRBYBLZBZCANCACCKCC"
    "CODCDCAFCFCOGCGCHECHCIVCICOKCKCHLCLCMRCMCHNCNCOLCOCRICRCUBCUCPVCVCUWCWCXRCXCYPCYCZECZDEUDEDJIDJDNKDK"
    "DMADMDOMDODZADZECUECESTEEEGYEGESHEHERIERESPESETHETFINFIFJIFJFLKFKFSMFMFROFOFRAFRGABGAGBRGBGRDGDGEOGE"
    "GUFGFGGYGGGHAGHGIBGIGRLGLGMBGMGINGNGLPGPGNQGQGRCGRSGSGSGTMGTGUMGUGNBGWGUYGYHKGHKHMDHMHNDHNHRVHRHTIHT"
    "HUNHUIDNIDIRLIEISRILIMNIMINDINIOTIOIRQIQIRNIRISLISITAITJEYJEJAMJMJORJOJPNJPKENKEKGZKGKHMKHKIRKICOMKM"
    "KNAKNPRKKPKORKRKWTKWCYMKYKAZKZLAOLALBNLBLCALCLIELILKALKLBRLRLSOLSLTULTLUXLULVALVLBYLYMARMAMCOMCMDAMD"
    "MNEMEMAFMFMDGMGMHLMHMKDMKMLIMLMMRMMMNGMNMACMOMNPMPMTQMQMRTMRMSRMSMLTMTMUSMUMDVMVMWIMWMEXMXMYSMYMOZMZ"
    "NAMNANCLNCNERNENFKNFNGANGNICNINLDNLNORNONPLNPNRUNRNIUNUNZLNZOMNOMPANPAPERPEPYFPFPNGPGPHLPHPAKPKPOLPL"
    "SPMPMPCNPNPRIPRPSEPSPRTPTPLWPWPRYPYQATQAREUREROUROSRBRSRUSRURWARWSAUSASLBSBSYCSCSDNSDSWESESGPSGSHNSH"
    "SVNSISJMSJSVKSKSLESLSMRSMSENSNSOMSOSURSRSSDSSSTPSTSLVSVSXMSXSYRSYSWZSZTCATCTCDTDATFTFTGOTGTHATHTJKTJ"
    "TKLTKTLSTLTKMTMTUNTNTONTOTURTRTTOTTTUVTVTWNTWTZATZUKRUAUGAUGUMIUMUSAUSURYUYUZBUZVATVAVCTVCVENVEVGBVG"
    "VIRVIVNMVNVUTVUWLFWFWSMWSXXKXKYEMYEMYTYTZAFZAZMBZMZWEZW";

/* The VW menu's language codes (its language_files) and the names of eSpeak's voices */
static const char *const languages[][2] = {
    { "bul", "bg" }, { "cze", "cs" }, { "dan", "da" }, { "dut", "nl" }, { "eng", "en" },
    { "fin", "fi" }, { "fre", "fr" }, { "ger", "de" }, { "gre", "el" }, { "hun", "hu" },
    { "ita", "it" }, { "lav", "lv" }, { "lit", "lt" }, { "nob", "no" }, { "pol", "pl" },
    { "por", "pt" }, { "rum", "ro" }, { "rus", "ru" }, { "scr", "hr" }, { "slo", "sk" },
    { "spa", "es" }, { "swe", "sv" }, { "tur", "tr" },
};

/* alpha-2 of an alpha-3 country code, or NULL */
static const char *iso2(const char *iso3)
{
    const char *p;

    for (p = iso3_iso2; *p; p += 5)
        if (!strncmp(p, iso3, 3))
            return p + 3;
    return NULL;
}

static void save_country(const char *two)
{
    HANDLE file;
    DWORD written;

    if (!strncmp(country, two, 2))
        return;
    memcpy(country, two, 2);
    CreateDirectoryW(DATA_DIR, NULL);
    file = CreateFileW(COUNTRY_TXT, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, country, 2, &written, NULL);
        CloseHandle(file);
    }
}

/* At start: the saved country; without one, the country of the VW menu's language. */
static void init_country(void)
{
    HANDLE file = CreateFileW(COUNTRY_TXT, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size = 0;
    const char *guess;

    if (file != INVALID_HANDLE_VALUE) {
        ReadFile(file, country, 2, &size, NULL);
        CloseHandle(file);
    }
    if (size != 2) {
        country[0] = 0;
        if (strlen(vw_language) == 7 && (guess = iso2(vw_language + 4)) != NULL)
            save_country(guess);
    }
}

/* A country from message 11011 (3 letters): keep it, also while Navit runs (border crossings). */
static void country_seen(const char *iso3)
{
    const char *two = iso2(iso3);

    if (two)
        save_country(two);
}

/* language="xx" in the <speech> line of navit.xml, for the VW menu's language. Navit has to read
 * it at start, so this runs before Navit does. The file is rewritten only when the line changes. */
static void set_voice_language(void)
{
    static char text[65536], out[65536 + 64];
    const char *voice = NULL, *tag, *end, *old;
    HANDLE file;
    DWORD size = 0;
    int i, n;

    for (i = 0; i < (int)(sizeof languages / sizeof *languages); i++)
        if (!strncmp(vw_language, languages[i][0], 3))
            voice = languages[i][1];
    if (!voice)
        return;                             /* not started from the menu, or no such voice */
    file = CreateFileW(NAVIT_XML, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    ReadFile(file, text, sizeof text - 1, &size, NULL);
    CloseHandle(file);
    text[size] = 0;
    if (size == sizeof text - 1 || !(tag = strstr(text, "<speech type=\"espeak\"")) || !(end = strchr(tag, '>')))
        return;
    old = strstr(tag, " language=\"");
    if (old && old < end) {                 /* replace the value */
        const char *value_end = strchr(old + 11, '"');
        if (!value_end || value_end > end || ((size_t)(value_end - old - 11) == strlen(voice)
                                              && !strncmp(old + 11, voice, strlen(voice))))
            return;                         /* malformed, or already right */
        n = sprintf(out, "%.*s language=\"%s\"%s", (int)(old - text), text, voice, value_end + 1);
    } else {                                /* add it after the type */
        const char *after = tag + strlen("<speech type=\"espeak\"");
        n = sprintf(out, "%.*s language=\"%s\"%s", (int)(after - text), text, voice, after);
    }
    file = CreateFileW(NAVIT_XML, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        write_text(file, out);
        CloseHandle(file);
    }
    (void)n;
}

/* ---- map files ---------------------------------------------------------------------- */

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

/* ---- the menu's overlays -------------------------------------------------------------- */

/*
 * The menu (VwUserShell) sends AppCom message 31 [31, state] to group Settings whenever the
 * winner of its priority flags changes (docs/system/user-shell.md section 4): 13 = nothing
 * shown, 11 = the radio is not in normal operation (shown nothing over Navit in the car), any
 * other value = an overlay or pop-up (1 incoming call, 4 parking sensors, 5 door picture, 6 call,
 * 8 screen off, ...). It brings its own window to the front for them and, when they end, goes
 * back to the program it knows (navigation, phone, media player), never to Navit.
 *
 * So: a timer notes when Navit was last in front; an overlay that starts within a moment of
 * that is remembered, and when the state goes back to 11 / 13 Navit is put in front again,
 * but only if the menu holds the screen then (not the phone program, say).
 */
#define WM_OVERLAY     (WM_APP + 4)     /* wparam = the menu's overlay state (AppCom message 31) */
#define WM_TO_MENU     (WM_APP + 5)     /* from "navit.exe menu": the user wants the VW menu */
#define WM_COUNTRY     (WM_APP + 6)     /* wparam = 3 letters: country of the GPS position (11011) */
#define TIMER_WATCH    1                /* every 500 ms while Navit runs: is it in front? */
#define TIMER_RESTORE  2                /* shortly after an overlay ended */
#define NAVIT_RECENT   2000             /* ms: Navit counts as "was in front" this long */

static HWND navit_window;               /* while Navit runs */
static DWORD navit_front_at;            /* GetTickCount() when Navit was last seen in front */
static BOOL overlay_over_navit, overlay_on;
static int restore_tries;

static BOOL overlay_state(int state)
{
    return state != 11 && state != 13;
}

/* Listener thread on the launcher's AppCom port: [31, Int state] -> WM_OVERLAY; navigon's
 * [11011, [speed, limit, 1, flag, country]], country = UTF-16 "ITA" + 0 -> WM_COUNTRY. The
 * groups' other messages are dropped. */
static DWORD WINAPI appcom_thread(LPVOID parameter)
{
    static const DWORD expected[] = { 0, 0, 4, 2, 8, 31, 8 };  /* header, list of 2: Int 31, Int */
    HWND window = parameter;
    struct sockaddr_in address;
    SOCKET udp = socket(AF_INET, SOCK_DGRAM, 0);
    DWORD datagram[1100];               /* the largest datagram: 8 + 4000 bytes */
    int length, i;

    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(APPCOM_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (udp == INVALID_SOCKET || bind(udp, (struct sockaddr *)&address, sizeof address))
        return 1;
    for (;;) {
        length = recvfrom(udp, (char *)datagram, sizeof datagram, 0, NULL, NULL);
        if (length < 8 + 6 * 4 || datagram[0] != 0)     /* one datagram, nothing following */
            continue;
        for (i = 2; i < 7 && datagram[i] == expected[i]; i++)
            ;
        if (i == 7 && length == 8 + 6 * 4)
            PostMessageW(window, WM_OVERLAY, (WPARAM)datagram[7], 0);
        else if (length == 80 && datagram[2] == 4 && datagram[3] == 2 && datagram[4] == 8
                 && datagram[5] == 11011 && datagram[6] == 4 && datagram[7] == 5
                 && datagram[16] == 6 && datagram[17] == 8) {
            const unsigned short *c = (const unsigned short *)&datagram[18];

            if (c[0] < 128 && c[1] < 128 && c[2] < 128 && c[3] == 0)
                PostMessageW(window, WM_COUNTRY, c[0] | c[1] << 8 | c[2] << 16, 0);
        }
    }
}

static BOOL menu_in_front(void)
{
    HWND menu = FindWindowW(L"VwUserShell", NULL), front = GetForegroundWindow();
    DWORD menu_process = 0, front_process = 1;

    if (!menu || !front)
        return FALSE;
    GetWindowThreadProcessId(menu, &menu_process);
    GetWindowThreadProcessId(front, &front_process);
    return menu_process == front_process;
}

static void overlay_changed(HWND window, int state)
{
    wchar_t line[64];

    wsprintfW(line, L"NAVIT: menu overlay state %d", state);
    OutputDebugStringW(line);
    if (!navit_window)
        return;
    if (overlay_state(state)) {
        if (!overlay_on)                /* the first of possibly several overlays in a row */
            overlay_over_navit = GetForegroundWindow() == navit_window
                                 || GetTickCount() - navit_front_at < NAVIT_RECENT;
        overlay_on = TRUE;
        KillTimer(window, TIMER_RESTORE);
    } else if (overlay_on) {
        overlay_on = FALSE;
        if (overlay_over_navit) {       /* the menu raises its window after it sends 31: wait */
            restore_tries = 4;
            SetTimer(window, TIMER_RESTORE, 500, NULL);
        }
    }
}

/* Show the VW menu. Navit stays where it is, behind it. */
static void show_menu(void)
{
    HWND menu = FindWindowW(L"VwUserShell", NULL);

    OutputDebugStringW(L"NAVIT: to the VW menu");
    if (menu)
        SetForegroundWindow(menu);
}

/* The user left Navit on purpose: an overlay now must not bring it back when it ends. */
static void to_menu(HWND window)
{
    navit_front_at = GetTickCount() - NAVIT_RECENT;
    overlay_over_navit = FALSE;
    KillTimer(window, TIMER_RESTORE);
    show_menu();
}

static void timer(HWND window, UINT id)
{
    if (!navit_window || !IsWindow(navit_window))
        return;
    if (id == TIMER_WATCH) {
        if (GetForegroundWindow() == navit_window)
            navit_front_at = GetTickCount();
        return;
    }
    /* TIMER_RESTORE: a few tries, 500 ms apart, in case the menu comes to the front late */
    if (menu_in_front()) {
        OutputDebugStringW(L"NAVIT: overlay ended, Navit back to the front");
        SetForegroundWindow(navit_window);
        InvalidateRect(navit_window, NULL, FALSE);
    }
    if (--restore_tries <= 0)
        KillTimer(window, TIMER_RESTORE);
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
    navit_window = navit;
    navit_front_at = GetTickCount();
    SetTimer(window, TIMER_WATCH, 500, NULL);
}

static void navit_done(HWND window)
{
    /* Navit shows the Windows CE taskbar when it exits; the unit has none, the emulator has */
    HWND taskbar = FindWindowW(L"HHTaskBar", NULL);

    if (taskbar)
        ShowWindow(taskbar, SW_HIDE);
    navit_window = NULL;
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
        set_voice_language();
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
    case WM_OVERLAY:
        overlay_changed(window, (int)wparam);
        return 0;
    case WM_TIMER:
        timer(window, (UINT)wparam);
        return 0;
    case WM_TO_MENU:
        to_menu(window);
        return 0;
    case WM_COUNTRY: {
        char iso3[4] = { (char)wparam, (char)(wparam >> 8), (char)(wparam >> 16), 0 };
        country_seen(iso3);
        return 0;
    }
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
    WSADATA winsock;
    LOGFONTW logfont;
    HWND window;
    HANDLE listener;
    HWND running;
    MSG msg;

    /* "navit.exe menu", from Navit's home button: hand over to the running launcher, which also
     * stops watching for overlays over Navit; without one, show the menu directly */
    while (*command_line == L' ')
        command_line++;
    if (!wcsncmp(command_line, L"menu", 4)) {
        running = FindWindowW(CLASS_NAME, TITLE);
        if (running)
            PostMessageW(running, WM_TO_MENU, 0, 0);
        else
            show_menu();
        return 0;
    }

    /* already open (Navit or this picker): bring it to the front */
    running = FindWindowW(L"NAVGRA", L"Navit");
    if (!running)
        running = FindWindowW(CLASS_NAME, TITLE);
    if (running) {
        SetForegroundWindow(running);
        return 0;
    }

    WSAStartup(MAKEWORD(2, 2), &winsock);
    {   /* the VW menu starts apps with " language=ita-ITA skin=VW" */
        const wchar_t *language = wcsstr(command_line, L"language=");
        if (language)
            WideCharToMultiByte(CP_ACP, 0, language + 9, 7, vw_language, sizeof vw_language - 1, NULL, NULL);
    }
    init_country();
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
    window = CreateWindowExW(WS_EX_TOPMOST, CLASS_NAME, TITLE, WS_POPUP | WS_VISIBLE,
                             0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                             NULL, NULL, instance, NULL);
    /* listen from the start: the country arrives while the user ticks the maps */
    listener = CreateThread(NULL, 0, appcom_thread, window, 0, NULL);
    if (listener)
        CloseHandle(listener);

    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DeleteObject(font);
    return (int)msg.wParam;
}
