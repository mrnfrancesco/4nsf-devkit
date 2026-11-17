/*
 * svoxdll.dll - stand-in for the SVOX text-to-speech decoder.
 *
 * On the unit this DLL is part of the system image and cannot be copied out in working
 * form. BT\spxdec.dll imports four functions from it; with this file next to it, it loads.
 * No speech is produced.
 */
#define EXPORT __declspec(dllexport)

EXPORT int SVOX_GetDecoderBytes(void)       { return 0; }
EXPORT int SVOX_PutDecoderSamples(void)     { return 0; }
EXPORT int SVOX_TTS_GetDecoderBytes(void)   { return 0; }
EXPORT int SVOX_TTS_PutDecoderSamples(void) { return 0; }
