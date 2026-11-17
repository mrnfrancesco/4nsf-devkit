/*
 * cardata - example: read live vehicle data. One window listing what the car says, twice a second.
 *
 * The unit's car-link program publishes every vehicle message in a block of shared memory
 * (apps/include/vwshm.h). This app copies the messages out, each under its mutex, and decodes
 * a few of them the way docs/car/can-signals.md describes ("shm" column of its tables).
 *
 * In the emulator the data comes from the simulated radio:  make run RADIO=1
 * Without it, the same program can play the car-link program and write messages itself:
 *
 *   cardata.exe set RadioInit=5756575A5A5A41415A46443030303030300100 Bsg3=03 GwMotor=1027B800
 *
 * (each: message name = data bytes in hex; then the menu is told that they changed). That is
 * "radio connected", "ignition on" and "2500 rpm, coolant 90 °C". Run it with
 * emulator/deploy.py run; more in docs/car/shared-memory.md, section 8.5.
 */
#include <windows.h>
#include <stddef.h>
#include <stdlib.h>
#include "vwshm.h"

#define CLASS_NAME L"CarData4NSF"
#define TITLE      L"CarData4NSF"

/*
 * The 26 vehicle messages: name (as in the mutex name "Message<name>Mutex"), place in the
 * shared memory, number of data bytes, and the change notifications the menu listens to for
 * that message (docs/car/shared-memory.md, section 4.3; only `set` uses them).
 */
#define WIDE(text) L ## text
#define BLOCK(name, field, ...) \
    { WIDE(#name), offsetof(VwSharedMemory, field), sizeof ((VwSharedMemory *)0)->field.data, { __VA_ARGS__ } }

static const struct block {
    const wchar_t *name;
    unsigned offset, size;
    unsigned char changes[4];
} blocks[] = {
    BLOCK(Bsg3, bsg3, 3, 4),            BLOCK(GwMotor, gwMotor, 2, 25, 28, 29),     BLOCK(FzgIdent, fzgIdent, 0),
    BLOCK(GwKombi, gwKombi, 21, 22),    BLOCK(BsgKombi, bsgKombi, 1),               BLOCK(Systeminfo1, systeminfo1, 13),
    BLOCK(Bsg2, bsg2, 23),              BLOCK(KombiK1, kombiK1, 8),                 BLOCK(Mfa1, mfa1, 14),
    BLOCK(Mfa2, mfa2, 15),              BLOCK(Mfa3, mfa3, 16),                      BLOCK(Mfa4, mfa4, 0),
    BLOCK(Mfa6, mfa6, 18),              BLOCK(Mfa7, mfa7, 19),                      BLOCK(Mfa8, mfa8, 20),
    BLOCK(MotorEv1, motorEv1, 26, 27),  BLOCK(RadioInit, radioInit, 11),            BLOCK(Gateway3, gateway3, 30),
    BLOCK(GwAnt2, gwAnt2, 2, 7),        BLOCK(MflTasten, mflTasten, 31),            BLOCK(Motor7, motor7, 24, 35),
    BLOCK(Zke4, zke4, 0),               BLOCK(Reichweite1, reichweite1, 32),        BLOCK(Reichweite2, reichweite2, 33),
    BLOCK(Hvem2, hvem2, 34),            BLOCK(GwAnt1, gwAnt1, 36, 37),
};
#define BLOCKS (sizeof blocks / sizeof blocks[0])

static VwSharedMemory *shared;          /* the unit's block; NULL while the car-link program is not running */
static VwSharedMemory car;              /* our copy of the 26 messages */

static BOOL attach(void)
{
    HANDLE mapping;

    if (shared)
        return TRUE;
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, VWSHM_SIZE, VWSHM_NAME);
    if (mapping && GetLastError() == ERROR_ALREADY_EXISTS)
        shared = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, VWSHM_SIZE);
    else if (mapping)
        CloseHandle(mapping);           /* we created it: nobody fills it. Do not keep an empty block alive */
    return shared != NULL;
}

static HANDLE lock(const wchar_t *prefix, const wchar_t *name)
{
    wchar_t full[64];
    HANDLE mutex;

    wsprintfW(full, L"%s%sMutex", prefix, name);
    mutex = CreateMutexW(NULL, FALSE, full);
    WaitForSingleObject(mutex, INFINITE);
    return mutex;
}

static void unlock(HANDLE mutex)
{
    ReleaseMutex(mutex);
    CloseHandle(mutex);
}

/* Copy every message into `car`, holding each mutex only for the copy. */
static void read_car(void)
{
    unsigned i;

    for (i = 0; i < BLOCKS; i++) {
        const struct block *block = &blocks[i];
        HANDLE mutex = lock(L"Message", block->name);

        memcpy((BYTE *)&car + block->offset, (BYTE *)shared + block->offset, block->size + (block->size + 7) / 8);
        unlock(mutex);
    }
}

/* ---- decoding: docs/car/can-signals.md ----------------------------------------------------- */

/* `length` bits starting at bit `start`, counted from the low bit of the first byte. */
static unsigned bits(const uint8_t *data, unsigned start, unsigned length)
{
    unsigned value = 0, i;

    for (i = 0; i < length; i++)
        value |= ((data[(start + i) / 8] >> ((start + i) % 8)) & 1u) << i;
    return value;
}

/* A number with one decimal: wsprintfW cannot print floating point. `tenths` may be negative. */
static const wchar_t *decimal(wchar_t *text, int tenths)
{
    wsprintfW(text, L"%s%d.%d", tenths < 0 ? L"-" : L"", abs(tenths) / 10, abs(tenths) % 10);
    return text;
}

#define LINES 8
static const wchar_t *const labels[LINES] = {
    L"radio", L"ignition", L"engine", L"speed", L"doors", L"now", L"since start", L"fuel",
};

static void describe(wchar_t line[LINES][100])
{
    static const wchar_t *const doors[6] = { L"driver ", L"passenger ", L"rear left ", L"rear right ", L"bonnet ", L"boot " };
    wchar_t a[16], b[16];
    int i;

    for (i = 0; i < LINES; i++)
        wcscpy(line[i], L"-");

    /* 0xF001, section 3.16: bytes 0..16 = VIN; valid once the radio's handshake was accepted */
    if (!VW_BYTE_VALID(car.radioInit, 0)) {
        wcscpy(line[0], L"not connected");
        return;                                             /* the unit drops all other messages until then */
    }
    wcscpy(line[0], L"connected, VIN ");
    for (i = 0; i < 17; i++)
        line[0][15 + i] = car.radioInit.data[i];
    line[0][15 + 17] = 0;

    /* 0x575, section 3.8: bit 0 = key in, bit 1 = ignition */
    if (VW_BYTE_VALID(car.bsg3, 0))
        wsprintfW(line[1], L"%s, key %s", car.bsg3.data[0] & 2 ? L"on" : L"off", car.bsg3.data[0] & 1 ? L"in" : L"out");

    /* 0x35B, section 3.1: bytes 0..1 = engine speed in 0.25 rpm, byte 2 = coolant, 0.75 °C steps from -48 */
    if (VW_BYTE_VALID(car.gwMotor, 0) && VW_BYTE_VALID(car.gwMotor, 2))
        wsprintfW(line[2], L"%d rpm, coolant %d \x00b0" L"C", bits(car.gwMotor.data, 0, 16) / 4,
                  car.gwMotor.data[2] * 3 / 4 - 48);
    /* 0x555, section 3.7: byte 0 = oil temperature, 1 °C steps from -60 */
    if (VW_BYTE_VALID(car.motor7, 0))
        wsprintfW(line[2] + wcslen(line[2]), L", oil %d \x00b0" L"C", car.motor7.data[0] - 60);

    /* 0x527, section 3.5: 15 bits from bit 9 = speed in 0.01 km/h, byte 3 = outside temperature, 0.5 °C steps from -50 */
    if (VW_BYTE_VALID(car.gwKombi, 1) && VW_BYTE_VALID(car.gwKombi, 3))
        wsprintfW(line[3], L"%s km/h, outside %s \x00b0" L"C", decimal(a, bits(car.gwKombi.data, 9, 15) / 10),
                  decimal(b, car.gwKombi.data[3] * 5 - 500));

    /* 0x470, section 3.4: byte 1, one bit per door */
    if (VW_BYTE_VALID(car.bsgKombi, 1)) {
        line[4][0] = 0;
        for (i = 0; i < 6; i++)
            if (car.bsgKombi.data[1] & (1 << i))
                wcscat(line[4], doors[i]);
        wcscat(line[4], line[4][0] ? L"open" : L"all shut");
    }

    /* trip computer, section 3.6. 0x629: consumption now (12 bits, tenths), range (14 bits from bit 16) */
    if (VW_BYTE_VALID(car.mfa1, 0) && VW_BYTE_VALID(car.mfa1, 2))
        wsprintfW(line[5], L"%s l/100 km, range %d km", decimal(a, bits(car.mfa1.data, 0, 12)), bits(car.mfa1.data, 16, 14));
    /* 0x62B: average consumption, distance, minutes (14 bits from bit 32), average speed (from bit 48) */
    if (VW_BYTE_VALID(car.mfa2, 0) && VW_BYTE_VALID(car.mfa2, 4))
        wsprintfW(line[6], L"%s l/100 km, %d km, %d min, %d km/h", decimal(a, bits(car.mfa2.data, 0, 12)),
                  bits(car.mfa2.data, 16, 14), bits(car.mfa2.data, 32, 14), bits(car.mfa2.data, 48, 14));

    /* 0x621, section 3.8: bit 1 = low-fuel warning; 0xF001 byte 0x11 = fuel type, 1 = petrol */
    if (VW_BYTE_VALID(car.kombiK1, 0))
        wsprintfW(line[7], L"%s%s", car.radioInit.data[0x11] == 1 ? L"petrol, " : L"",
                  car.kombiK1.data[0] & 2 ? L"low!" : L"enough");
}

/* ---- the window --------------------------------------------------------------------------------- */

static void paint(HDC screen, const RECT *area)
{
    wchar_t line[LINES][100];
    int i, row = (area->bottom - 40) / LINES;
    RECT cell = *area;

    cell.bottom = 36;
    DrawTextW(screen, L"cardata - vehicle data from the shared memory - tap to exit", -1, &cell,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (!attach()) {
        cell = *area;
        DrawTextW(screen, L"VwCommunicationExe is not running", -1, &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    read_car();
    describe(line);
    for (i = 0; i < LINES; i++) {
        SetRect(&cell, 16, 40 + i * row, 110, 40 + (i + 1) * row);
        DrawTextW(screen, labels[i], -1, &cell, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetRect(&cell, 110, 40 + i * row, area->right, 40 + (i + 1) * row);
        DrawTextW(screen, line[i], -1, &cell, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        SetTimer(window, 1, 500, NULL);
        return 0;
    case WM_TIMER:
        InvalidateRect(window, NULL, TRUE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint_state;
        RECT area;
        HDC screen = BeginPaint(window, &paint_state);

        GetClientRect(window, &area);
        paint(screen, &area);
        EndPaint(window, &paint_state);
        return 0;
    }
    case WM_LBUTTONUP:              /* the touch screen sends mouse messages */
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

/* ---- cardata.exe set Name=hex ... ---------------------------------------------------------------- */

static int hex_digit(wchar_t c)
{
    return c >= L'0' && c <= L'9' ? c - L'0' : (c | 0x20) >= L'a' && (c | 0x20) <= L'f' ? (c | 0x20) - L'a' + 10 : -1;
}

/* Tell the menu (listener 1) that item `change` is new: set its bit, then wake the listener. */
static void notify(unsigned change)
{
    HANDLE mutex = lock(L"", L"Notifications");
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, L"CAN_DATA_EVENT1");

    shared->pending[1][change >> 3] |= 1 << (change & 7);
    unlock(mutex);
    SetEvent(event);
    CloseHandle(event);
}

/* Write the messages named in `text` ("Name=hex Name=hex ..."). Returns how many were written. */
static int set(wchar_t *text)
{
    int written = 0;
    unsigned i, n;

    while (*text) {
        wchar_t *name = text, *value = wcschr(text, L'=');

        if (!value)
            break;
        *value++ = 0;
        for (i = 0; i < BLOCKS && _wcsicmp(blocks[i].name, name); i++)
            ;
        if (i < BLOCKS) {
            const struct block *block = &blocks[i];
            BYTE *data = (BYTE *)shared + block->offset;
            HANDLE mutex = lock(L"Message", block->name);

            for (n = 0; n < block->size && hex_digit(value[0]) >= 0 && hex_digit(value[1]) >= 0; n++, value += 2) {
                data[n] = hex_digit(value[0]) << 4 | hex_digit(value[1]);
                data[block->size + n / 8] &= ~(1 << (n % 8));           /* this byte is valid now */
            }
            unlock(mutex);
            for (n = 0; n < 4 && block->changes[n]; n++)
                notify(block->changes[n]);
            written++;
        }
        while (*value && *value != L' ')
            value++;
        while (*value == L' ')
            value++;
        text = value;
    }
    return written;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show)
{
    WNDCLASSW window_class;
    MSG message;

    if (!wcsncmp(command_line, L"set ", 4))                 /* exit code 0: at least one message written */
        return attach() && set(command_line + 4) ? 0 : 1;

    memset(&window_class, 0, sizeof window_class);
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    window_class.lpszClassName = CLASS_NAME;
    RegisterClassW(&window_class);

    CreateWindowExW(WS_EX_TOPMOST, CLASS_NAME, TITLE, WS_POPUP | WS_VISIBLE,
                    0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                    NULL, NULL, instance, NULL);

    while (GetMessageW(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
