/*
 * cestub.dll - system functions the emulator's Windows does not have.
 *
 * The unit's programs import these from COREDLL (Windows CE 6). The Windows Embedded
 * Compact 7 kernel used by the emulator does not export them, so build.sh points those
 * imports here instead (patches/redirect_imports.py, which also checks this list is complete).
 *
 * No <windows.h>: its declarations of the same names would clash with these stand-ins.
 */
#define EXPORT __declspec(dllexport)

/* Returns 0 whatever the arguments. The software copes: it never gets a connection. */
#define ALWAYS_ZERO(name) EXPORT int name(void) { return 0; }

/* dial-up networking (RAS): navigon.exe, BTCore.exe */
ALWAYS_ZERO(RasDial)
ALWAYS_ZERO(RasEnumConnections)
ALWAYS_ZERO(RasEnumEntries)
ALWAYS_ZERO(RasGetConnectStatus)
ALWAYS_ZERO(RasGetEntryProperties)
ALWAYS_ZERO(RasGetLinkStatistics)
ALWAYS_ZERO(RasHangUp)
ALWAYS_ZERO(RasSetEntryDialParams)
ALWAYS_ZERO(RasSetEntryProperties)

/* telephony (TAPI): BTCore.exe */
ALWAYS_ZERO(lineDevSpecific)
ALWAYS_ZERO(lineGetDevCaps)
ALWAYS_ZERO(lineGetDevConfig)
ALWAYS_ZERO(lineInitialize)
ALWAYS_ZERO(lineOpen)
ALWAYS_ZERO(lineShutdown)

/*
 * Power status: used by every hardware-layer DLL, and this one must answer properly.
 * The callers keep the 24-byte SYSTEM_POWER_STATUS_EX on their stack, so write exactly that.
 */
struct power_status {
    unsigned char ac_line;              /* 1 = on external power */
    unsigned char battery_flag;         /* 1 = high */
    unsigned char battery_percent;
    unsigned char reserved1;
    unsigned int  battery_seconds;      /* 0xFFFFFFFF = unknown */
    unsigned int  battery_full_seconds;
    unsigned char reserved2;
    unsigned char backup_flag;
    unsigned char backup_percent;
    unsigned char reserved3;
    unsigned int  backup_seconds;
    unsigned int  backup_full_seconds;
};
_Static_assert(sizeof(struct power_status) == 24, "must match SYSTEM_POWER_STATUS_EX");

EXPORT int GetSystemPowerStatusEx(struct power_status *status, int update)
{
    static const struct power_status on_charger_battery_full = {
        1, 1, 100, 0, 0xFFFFFFFF, 0xFFFFFFFF,
        0, 1, 100, 0, 0xFFFFFFFF, 0xFFFFFFFF,
    };
    *status = on_charger_battery_full;
    return 1;
}
