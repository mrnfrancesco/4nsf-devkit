/*
 * ddraw.dll - stand-in for DirectDraw, which the emulator's Windows does not have.
 *
 * navigon.exe loads the unit's OpenGL ES DLL, which imports DirectDrawCreate. Failing the
 * call is enough: the DLL loads, and Navigon is configured to use its software map
 * renderer instead (OldMapDrawer in build.sh).
 */
#define EXPORT __declspec(dllexport)
#define E_FAIL 0x80004005

EXPORT int DirectDrawCreate(void *guid, void **direct_draw, void *outer)
{
    return E_FAIL;
}
