/*
 * WolfSpecial.dll - stand-in for the unit's board-support DLL ("Wolf" = the hardware platform).
 *
 * The real one talks to drivers of the unit's own board: hidden flash, hardware watchdog,
 * backlight, audio mute pin, Bluetooth chip. This one answers like a unit that sits in its
 * cradle in a petrol Volkswagen up! and is already paired with it. Same 36 exports, same
 * prototypes and return values as the real DLL (docs/system/hardware-layer.md, section 2).
 *
 * Hidden flash: on the unit, 2 KB of raw disk sectors. Here it is the file FLASH_FILE on the
 * flash disk, read and rewritten as a whole on every call (as the real DLL does with its
 * sectors), under a named mutex. So all processes see the same contents, and what one of
 * them writes is still there after a restart of the emulator. While the file does not exist
 * the contents are the defaults below; delete the file to get them back.
 *
 * Everything else that can be set (brightness, speaker, watchdog) is only remembered inside
 * the calling process; functions that would touch hardware succeed without doing anything.
 *
 * The first version of this file and the hidden-flash layout come from the upstream project,
 * smn/4nsf-toolbox (NOTICE.md).
 */
#include <windows.h>

#define EXPORT __declspec(dllexport)

/* ---- hidden flash ---------------------------------------------------------------------
 *
 * Settings that survive a factory reset. Layout, from the read/write call sites in
 * VwBrandDevice.exe and VwUserShell.exe (docs/system/brand-and-pairing.md, section 2):
 *
 *   0x00   8  Navigon password lock, 0x08: its check copy (all zero = no password)
 *   0x10  13  "123Kuckuck123"  validity signature; without it the area is re-initialised
 *   0x1e  17  VIN of the current car
 *   0x2f   1  brand            0 Volkswagen, 2 Seat, 3 Skoda (selects skin and splash screen)
 *   0x30   1  'B'              branded; anything else shows "Connect your device to your
 *                              brand new car and switch the ignition on" and waits
 *   0x31   1  fuel bits        0x20 petrol, 0x04 diesel, 0x22 CNG, 0x08 electric, 0x0c hybrid
 *   0x32   1  vehicle class    0 A000 (XL1), 1 A00 (up!, Mii, Citigo), 2 A0 (Polo, Ibiza, Fabia)
 *   0x33   1  equipment line   0 none, 1 Cupra
 *   0x34   1  sound system     1 = present ("Fender" boot screens)
 *   0x35   1  body type        0 hatchback ... 9 other
 *   0x36  17  VIN of the first car the unit was paired with
 */
#define FLASH_SIZE  0x800
#define FLASH_FILE  L"\\My Flash Disk\\dev\\hidden-flash.bin"
#define FLASH_MUTEX L"WolfSpecialHiddenFlash"

#ifndef VIN
#define VIN "WVWZZZAAZFD000000"         /* placeholder; override with  make emulator VIN=... */
#endif
_Static_assert(sizeof(VIN) == 17 + 1, "a VIN has 17 characters");

/* A plain Volkswagen up!: petrol, no equipment line, no sound system, hatchback. */
static void default_flash(BYTE *flash)
{
    memset(flash, 0, FLASH_SIZE);
    memcpy(flash + 0x10, "123Kuckuck123", 13);
    memcpy(flash + 0x1e, VIN, 17);
    flash[0x2f] = 0;                    /* Volkswagen */
    flash[0x30] = 'B';                  /* branded */
    flash[0x31] = 0x20;                 /* petrol */
    flash[0x32] = 1;                    /* A00 = up! */
    flash[0x33] = 0;
    flash[0x34] = 0;
    flash[0x35] = 0;
    memcpy(flash + 0x36, VIN, 17);
}

static HANDLE lock_flash(void)
{
    HANDLE mutex = CreateMutexW(NULL, FALSE, FLASH_MUTEX);

    WaitForSingleObject(mutex, INFINITE);
    return mutex;
}

static void unlock_flash(HANDLE mutex)
{
    ReleaseMutex(mutex);
    CloseHandle(mutex);
}

static void load_flash(BYTE *flash)
{
    DWORD size = 0;
    HANDLE file = CreateFileW(FLASH_FILE, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    if (file != INVALID_HANDLE_VALUE) {
        ReadFile(file, flash, FLASH_SIZE, &size, NULL);
        CloseHandle(file);
    }
    if (size != FLASH_SIZE)
        default_flash(flash);
}

static BOOL save_flash(const BYTE *flash)
{
    DWORD size = 0;
    HANDLE file = CreateFileW(FLASH_FILE, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    WriteFile(file, flash, FLASH_SIZE, &size, NULL);
    CloseHandle(file);
    return size == FLASH_SIZE;
}

/* Both return the number of bytes copied, 0 on failure. */
EXPORT DWORD ReadHiddenFlash(BYTE *buffer, DWORD length, DWORD offset)
{
    BYTE flash[FLASH_SIZE];
    HANDLE lock;

    if (offset > FLASH_SIZE || length > FLASH_SIZE - offset)
        return 0;
    lock = lock_flash();
    load_flash(flash);
    unlock_flash(lock);
    memcpy(buffer, flash + offset, length);
    return length;
}

EXPORT DWORD WriteHiddenFlash(const BYTE *buffer, DWORD length, DWORD offset)
{
    BYTE flash[FLASH_SIZE];
    HANDLE lock;
    BOOL saved;

    if (offset > FLASH_SIZE || length > FLASH_SIZE - offset)
        return 0;
    lock = lock_flash();
    load_flash(flash);
    memcpy(flash + offset, buffer, length);
    saved = save_flash(flash);
    unlock_flash(lock);
    return saved ? length : 0;
}

/* ---- identification ---------------------------------------------------------------------
 *
 * Factory data on the unit. Each function wants a buffer of at least the size checked here
 * and returns the length of the text (GetDeviceID: always 10), 0 on failure.
 */
#define DEVICE_ID 1234567890u           /* made up. The unit prints its 32-bit number in decimal;
                                           it goes into the handshake with the radio (F002) */

static DWORD answer(wchar_t *text, DWORD length, DWORD needed, const wchar_t *value)
{
    if (!text || length < needed)
        return 0;
    wcscpy(text, value);
    return wcslen(text);
}

EXPORT DWORD GetDeviceID(wchar_t *text, DWORD length)
{
    if (!text || length < 10)
        return 0;
    wsprintfW(text, L"%u", DEVICE_ID);
    return 10;
}

EXPORT DWORD GetOSVersion(wchar_t *text, DWORD length)          { return answer(text, length, 8, L"2.33"); }
EXPORT DWORD GetBootLoaderVersion(wchar_t *text, DWORD length)  { return answer(text, length, 0x40, L"1.0"); }
EXPORT DWORD GetBTVersion(wchar_t *text, DWORD length)          { return answer(text, length, 0x40, L"1.0"); }

/* what: 0 memory vendor, 1 power chip vendor, 2 processor, 3 board revision */
EXPORT DWORD GetHWVersion(DWORD what, wchar_t *text, DWORD length)
{
    return answer(text, length, 0x14, what == 3 ? L"PCB V0" : L"QEMU");
}

/* A made-up, locally administered address: there is no Bluetooth chip. */
EXPORT BOOL ReadBTAddressFromHF(BYTE *address, DWORD length)
{
    static const BYTE fixed[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };

    if (!address || length != 6)
        return FALSE;
    memcpy(address, fixed, 6);
    return TRUE;
}

EXPORT BOOL WriteBTAddressToHF(const BYTE *address, DWORD length) { return address && length == 6; }

/* ---- power and cradle ---------------------------------------------------------------------
 *
 * Power source: 0 none (battery), 1 USB port of a computer, 2 USB charger, 3 the cradle's
 * supply pins, 4 other USB charger types. The unit's software treats 2 and 3 as "the proper
 * charger" and 1 and 2 as "USB". A unit in the car reports 3 and "in the cradle"; together
 * with cestub.dll's "on external power, battery full" the menu sees no reason for a battery
 * warning or a standby countdown (docs/system/user-shell.md, section 5.1).
 */
EXPORT int  GetPowerSource(void)          { return 3; }
EXPORT BOOL IsCradleStatusConnected(void) { return TRUE; }

/* ---- settings the software reads back: remembered per process ------------------------------ */

static DWORD brightness = 80;           /* percent */
static BOOL  display_on = TRUE;
static BOOL  speaker_off = TRUE;        /* amplifier of the built-in speaker: off while in the cradle */
static BOOL  line_muted = FALSE;        /* "external audio mute" pin of the dock */
static BOOL  watchdog_on = FALSE;
static DWORD watchdog_ms = 5000;        /* the driver's default */

EXPORT int   GetDeviceBrightness(void)             { return brightness; }
EXPORT BOOL  SetDeviceBrightness(DWORD percent)    { brightness = percent > 100 ? 100 : percent; return TRUE; }
EXPORT BOOL  GetDisplayState(void)                 { return display_on; }
EXPORT BOOL  SetDisplayState(BOOL on)              { display_on = on != 0; return TRUE; }
EXPORT BOOL  GetSpeakerMode(void)                  { return speaker_off; }
EXPORT BOOL  SetSpeakerMode(BOOL off)              { speaker_off = off != 0; return TRUE; }
EXPORT BOOL  GetExternalAudioMutePin(void)         { return line_muted; }
EXPORT BOOL  SetExternalAudioMutePin(BOOL mute)    { line_muted = mute == 1; return TRUE; }
EXPORT BOOL  GetHWWatchdogState(void)              { return watchdog_on; }
EXPORT BOOL  SetHWWatchdogState(BOOL on)           { watchdog_on = on != 0; return TRUE; }
EXPORT DWORD GetHWWatchdogTimeout(void)            { return watchdog_ms; }
EXPORT BOOL  SetHWWatchdogTimeout(DWORD ms)        { watchdog_ms = ms; return TRUE; }

/* ---- no effect here ------------------------------------------------------------------------- */

#define ALWAYS_TRUE(name)  EXPORT BOOL name(void) { return TRUE; }
#define ALWAYS_FALSE(name) EXPORT BOOL name(void) { return FALSE; }
#define NOTHING(name)      EXPORT void name(void) { }

ALWAYS_TRUE(RefreshHWWatchdog)          /* nothing bites: the emulator has no hardware watchdog */
ALWAYS_TRUE(SetLoaderTextColor)         /* boot loader pictures and text colour: on the unit   */
ALWAYS_TRUE(SetNoSWFoundImagePath)      /* these write raw disk sectors                         */
ALWAYS_TRUE(SetSplashImagePath)
ALWAYS_TRUE(SetUSBModeImagePath)
ALWAYS_TRUE(SetTCPWindowSize)
ALWAYS_TRUE(TouchCalibration)           /* the unit shows the calibration screen */

NOTHING(BackToFactorySettings)          /* the unit marks itself for a wipe and reboots */
NOTHING(ResetBTChip)

ALWAYS_FALSE(Gpio_control)              /* fails on the unit too (a bug in its argument check) */
ALWAYS_FALSE(GetGPRSModemPowerState)    /* no modem in this product: these fail on the unit */
ALWAYS_FALSE(SetGPRSModemPowerState)
ALWAYS_FALSE(ResetGPRSModemState)
