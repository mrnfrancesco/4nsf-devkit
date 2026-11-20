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

/* group Volume in the unit's registry (docs/apps/appcom.md section 6.3): navigon, phone,
 * menu, media player, picture viewer */
static const unsigned short volume_group[] = { 27011, 27015, 27017, 27018, 27019 };

static CRITICAL_SECTION lock;
static WAVEHDR *pending[MAX_PENDING];   /* written, not yet played */
static int pending_count;
static BOOL claimed;
static DWORD last_sound;                /* GetTickCount() of the last write or played buffer */
static SOCKET udp = INVALID_SOCKET;

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
    if (reason == DLL_PROCESS_ATTACH)
        InitializeCriticalSection(&lock);
    return TRUE;
}
