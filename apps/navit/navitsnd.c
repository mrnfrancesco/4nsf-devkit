/*
 * navitsnd.dll - Navit's spoken directions on the car's speakers.
 *
 * Docked, the unit is heard only through the car radio, and the radio plays the unit only when
 * the VW menu asks it to. The menu does that for whoever announces a sound with AppCom message
 * 29 ("sound manager", docs/system/media-audio.md section 5): type 2 = navigation advice makes
 * it ask for the radio's announcement channel. The unit's own navigation sends that message
 * around every prompt; Navit knows nothing about it.
 *
 * Navit's built-in eSpeak keeps one waveOut stream open and writes buffers to it only while it
 * speaks. apps/navit/fetch.sh patches Navit's navit.exe so that its waveOutWrite comes from
 * this DLL (emulator/patches/redirect_imports.py --only). Here:
 *   - first buffer after silence: send the claim [29, [PRIO, 2, 0, -1, -1]], wait CLAIM_LEAD ms
 *     for the radio to switch, then pass the buffer on to the real waveOutWrite;
 *   - a thread watches the buffers; RELEASE_AFTER ms after the last one has played it sends
 *     the release [29, [PRIO, 2, 1, -1, -1]].
 *
 * PRIO: the receivers keep one entry per priority (lower wins), so Navit must not share the
 * unit's navigation's 4: one navigation's release would cancel the other's claim. 2 is unused:
 * Navit beats the media player (5) and loses to a phone call (1).
 *
 * Plain sockets, no AppCom DLL. Messages go to the shipped members of group Volume.
 *
 * Second job, since this DLL runs inside Navit anyway: the default country of the town search.
 * Navit's country_default() returns a cached value when there is one, and in this Navit build
 * (navit.exe pinned by SHA-256 in fetch.sh) nothing else ever sets it, so Navit asks for the
 * country before every search. A thread here fills that cache with the country the launcher keeps
 * in data\country.txt (from the unit's navigation's GPS) and refreshes it every 30 s. It touches
 * Navit only after checking that country_default's code is exactly where expected.
 */
#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h>

#define OWN_PORT       27114            /* Navit's <Port> in apps/navit/NgAppInfo.xml */
#define PRIO           2
#define SOUND_ADVICE   2
#define CLAIM_LEAD     300              /* ms between claim and the first sound */
#define RELEASE_AFTER  1000             /* ms of silence before the release */
#define MAX_PENDING    32

#define COUNTRY_TXT          L"\\My Flash Disk\\Navit\\data\\country.txt"   /* 2 letters, by the launcher */
#define COUNTRY_DEFAULT_RVA  0x5314c        /* country_default() in navit.exe */
#define COUNTRY_ATTR_RVA     0x1270ec       /* its static struct attr { type, u.str } */
#define ATTR_COUNTRY_ISO2    0x30007        /* attr_country_iso2 in this build's attr_def.h */
#define COUNTRY_REFRESH      30000          /* ms */

/* group Volume in the unit's registry (docs/apps/appcom.md section 6.3): navigon, phone,
 * menu, media player, picture viewer */
static const unsigned short volume_group[] = { 27011, 27015, 27017, 27018, 27019 };

static CRITICAL_SECTION lock;
static WAVEHDR *pending[MAX_PENDING];   /* written, not yet played */
static int pending_count;
static BOOL claimed;
static DWORD last_sound;                /* GetTickCount() of the last write or played buffer */
static SOCKET udp = INVALID_SOCKET;

/* country_default(): ldr r3, =attr; ldr r0, [r3, #4]; cmp r0, #0; movne r0, r3; bx lr; then the
 * literal, the address of the attr */
static const DWORD country_default_code[] = { 0xe59f300c, 0xe5930004, 0xe3500000, 0x11a00003, 0xe12fff1e };
static char default_country[3];

/* Navit's load address if country_default() is where expected there, else NULL. Each candidate
 * is probed with IsBadReadPtr first: a wrong guess must not crash Navit. */
static BYTE *navit_base(void)
{
    BYTE *candidates[] = { (BYTE *)GetModuleHandleW(NULL), (BYTE *)0x10000 /* image base */ };
    int i;

    for (i = 0; i < 2; i++) {
        DWORD *code = (DWORD *)(candidates[i] + COUNTRY_DEFAULT_RVA);

        if (!IsBadReadPtr(code, sizeof country_default_code + 4)
            && !memcmp(code, country_default_code, sizeof country_default_code)
            && code[5] == (DWORD)(candidates[i] + COUNTRY_ATTR_RVA)
            && !IsBadWritePtr(candidates[i] + COUNTRY_ATTR_RVA, 8))
            return candidates[i];
    }
    return NULL;
}

static DWORD WINAPI country(LPVOID unused)
{
    BYTE *base = navit_base();
    DWORD *attr;
    wchar_t note[64];

    if (!base) {
        OutputDebugStringW(L"NAVITSND: not the expected navit.exe, no default country");
        return 1;
    }
    wsprintfW(note, L"NAVITSND: navit.exe at %08x", (unsigned)base);
    OutputDebugStringW(note);
    attr = (DWORD *)(base + COUNTRY_ATTR_RVA);
    for (;;) {
        char two[2];
        DWORD size = 0;
        HANDLE file = CreateFileW(COUNTRY_TXT, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                  OPEN_EXISTING, 0, NULL);

        if (file != INVALID_HANDLE_VALUE) {
            ReadFile(file, two, 2, &size, NULL);
            CloseHandle(file);
        }
        if (size == 2 && two[0] >= 'A' && two[0] <= 'Z' && two[1] >= 'A' && two[1] <= 'Z'
            && (two[0] != default_country[0] || two[1] != default_country[1])) {
            default_country[0] = two[0];
            default_country[1] = two[1];
            if (!attr[1]) {
                attr[0] = ATTR_COUNTRY_ISO2;
                attr[1] = (DWORD)default_country;
            }
            OutputDebugStringW(L"NAVITSND: default country set");
        }
        Sleep(COUNTRY_REFRESH);
    }
    return 0;
}

/* [29, [PRIO, SOUND_ADVICE, released, -1, -1]] to every member of group Volume */
static void announce(int released)
{
    /* datagram header {0 more to follow, own port}, then List(2): Int 29, List(5): five Ints */
    DWORD datagram[] = { 0, OWN_PORT, 4, 2, 8, 29, 4, 5,
                         8, PRIO, 8, SOUND_ADVICE, 8, released, 8, (DWORD)-1, 8, (DWORD)-1 };
    struct sockaddr_in to;
    int i;

    if (udp == INVALID_SOCKET)
        return;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for (i = 0; i < (int)(sizeof volume_group / sizeof *volume_group); i++) {
        to.sin_port = htons(volume_group[i]);
        sendto(udp, (char *)datagram, sizeof datagram, 0, (struct sockaddr *)&to, sizeof to);
    }
    OutputDebugStringW(released ? L"NAVITSND: release" : L"NAVITSND: claim");
}

/* Forget the buffers the driver has played; release after a quiet moment. */
static DWORD WINAPI watch(LPVOID unused)
{
    for (;;) {
        int i;

        Sleep(100);
        EnterCriticalSection(&lock);
        for (i = 0; i < pending_count; )
            if (pending[i]->dwFlags & WHDR_DONE)
                pending[i] = pending[--pending_count];
            else
                i++;
        if (pending_count)
            last_sound = GetTickCount();
        else if (claimed && GetTickCount() - last_sound >= RELEASE_AFTER) {
            claimed = FALSE;
            announce(1);
        }
        LeaveCriticalSection(&lock);
    }
    return 0;
}

/* Exported as waveOutWrite (navitsnd.def). */
MMRESULT WINAPI navitsnd_waveOutWrite(HWAVEOUT wave, LPWAVEHDR header, UINT size)
{
    static BOOL started;
    BOOL wait = FALSE;
    MMRESULT result;
    int i;

    EnterCriticalSection(&lock);
    if (!started) {                     /* first call: socket and watcher, outside DllMain */
        WSADATA winsock;
        HANDLE thread;

        started = TRUE;
        WSAStartup(MAKEWORD(2, 2), &winsock);
        udp = socket(AF_INET, SOCK_DGRAM, 0);
        thread = CreateThread(NULL, 0, watch, NULL, 0, NULL);
        if (thread)
            CloseHandle(thread);
    }
    if (!claimed) {
        claimed = TRUE;
        wait = TRUE;
        announce(0);
    }
    last_sound = GetTickCount();
    LeaveCriticalSection(&lock);

    if (wait)
        Sleep(CLAIM_LEAD);
    result = waveOutWrite(wave, header, size);

    /* after the real call: a reused buffer keeps its old WHDR_DONE until waveOutWrite clears it */
    EnterCriticalSection(&lock);
    for (i = 0; i < pending_count && pending[i] != header; i++)
        ;
    if (result == MMSYSERR_NOERROR && i == pending_count && pending_count < MAX_PENDING)
        pending[pending_count++] = header;
    last_sound = GetTickCount();
    LeaveCriticalSection(&lock);
    return result;
}

BOOL WINAPI DllMain(HANDLE module, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE thread;

        InitializeCriticalSection(&lock);
        thread = CreateThread(NULL, 0, country, NULL, 0, NULL);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
