/*
 * vwshm.h - the block of shared memory in which the unit publishes what the car says.
 *
 * VwCommunicationExe.exe owns the car link and writes every vehicle message it receives into
 * the named file mapping VWSHM_NAME; the menu and the other programs read it from there.
 * Your app can do the same. The layout below is fixed: no header, no version, the same in
 * every program of the unit. Where it comes from and the rules for using it:
 * docs/car/shared-memory.md. What the bytes of each message mean: docs/car/can-signals.md.
 *
 * How to read (apps/cardata is a complete example):
 *
 *   map the block:    CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, VWSHM_SIZE, VWSHM_NAME)
 *                     then MapViewOfFile. If GetLastError() is not ERROR_ALREADY_EXISTS the
 *                     car-link program is not running yet: close the handle, try again later.
 *   copy a message:   hold its mutex only for the copy. The mutex of gwMotor is named
 *                     "MessageGwMotorMutex": "Message", the field name capitalised, "Mutex".
 *   check each byte:  VW_BYTE_VALID(copy, n). Everything is invalid until the car has sent it.
 *   poll:             do not wait on the CAN_DATA_EVENT events and do not touch pending[]:
 *                     they belong to the unit's own programs.
 */
#ifndef VWSHM_H
#define VWSHM_H

#include <stdint.h>

#define VWSHM_NAME L"VwSharedMemoryFileMapping"
#define VWSHM_SIZE 0x61B0

#pragma pack(push, 1)

/* One vehicle message: its data bytes, then one "not valid" bit per data byte. */
#define VW_MESSAGE(type, size) typedef struct { uint8_t data[size]; uint8_t invalid[((size) + 7) / 8]; } type
VW_MESSAGE(VwMessage1, 1);   VW_MESSAGE(VwMessage2, 2);   VW_MESSAGE(VwMessage3, 3);   VW_MESSAGE(VwMessage4, 4);
VW_MESSAGE(VwMessage5, 5);   VW_MESSAGE(VwMessage6, 6);   VW_MESSAGE(VwMessage7, 7);   VW_MESSAGE(VwMessage8, 8);
VW_MESSAGE(VwMessage9, 9);   VW_MESSAGE(VwMessage12, 12); VW_MESSAGE(VwMessage17, 17); VW_MESSAGE(VwMessage20, 20);

#define VW_BYTE_VALID(message, byte) (!((message).invalid[(byte) >> 3] & (1u << ((byte) & 7))))

typedef struct {
    /*                            offset  contents                                      mutex            */
    uint8_t     pending[8][20];   /* 0x0000  change notifications: 8 listeners x 160 bits  NotificationsMutex */
    uint8_t     bapLsg0A[0x2C];   /* 0x00A0  parking sensors                                BapAllMutex, also  */
    uint8_t     bapLsg31[0x1064]; /* 0x00CC  radio: source, stations, texts, volume        for the six blocks */
    uint8_t     bapLsg2D[0xAC];   /* 0x1130  audio channel and sound settings              that follow        */
    uint8_t     bapLsg1D[0x24];   /* 0x11DC  electric drive status                                            */
    uint8_t     bapLsg11[0x94];   /* 0x1200  the car's clock                                                  */
    uint8_t     bapLsg25[0x4DCC]; /* 0x1294  charging (e-up!)                                                 */
    uint8_t     control[0x80];    /* 0x6060  state of the requests to the car                                 */
    /*                            offset  CAN id  contents (docs/car/can-signals.md)                         */
    VwMessage1  bsg3;             /* 0x60E0  0x575   key in, ignition                                         */
    VwMessage4  gwMotor;          /* 0x60E2  0x35B   engine speed, coolant temperature                        */
    VwMessage17 fzgIdent;         /* 0x60E7  0x65F   VIN as sent on CAN                                       */
    VwMessage12 gwKombi;          /* 0x60FB  0x527   speed, outside temperature, distance                     */
    VwMessage2  bsgKombi;         /* 0x6109  0x470   doors, bonnet, boot                                      */
    VwMessage3  systeminfo1;      /* 0x610C  0x651   make, class and body of the car                          */
    VwMessage1  bsg2;             /* 0x6110  0x571                                                            */
    VwMessage1  kombiK1;          /* 0x6112  0x621   low-fuel warning                                         */
    VwMessage8  mfa1;             /* 0x6114  0x629   trip computer: consumption now, range                    */
    VwMessage8  mfa2;             /* 0x611D  0x62B   trip computer: since start                               */
    VwMessage8  mfa3;             /* 0x6126  0x62D   trip computer: long term                                 */
    VwMessage6  mfa4;             /* 0x612F  0x529   trip computer: since refuelling                          */
    VwMessage9  mfa6;             /* 0x6136  0x52D   trip computer of electric cars, also mfa7, mfa8          */
    VwMessage8  mfa7;             /* 0x6141  0x52F                                                            */
    VwMessage8  mfa8;             /* 0x614A  0x532                                                            */
    VwMessage8  motorEv1;         /* 0x6153  0x61A   electric drive                                           */
    VwMessage20 radioInit;        /* 0x615C  0xF001  the radio's handshake: VIN, fuel type, equipment         */
    VwMessage6  gateway3;         /* 0x6173  0x653                                                            */
    VwMessage8  gwAnt2;           /* 0x617A  0x369   gear                                                     */
    VwMessage7  mflTasten;        /* 0x6183  0x5C1   steering-wheel keys                                      */
    VwMessage2  motor7;           /* 0x618B  0x555   oil temperature, boost pressure                          */
    VwMessage1  zke4;             /* 0x618E  0x591   central locking                                          */
    VwMessage7  reichweite1;      /* 0x6190  0x668   ranges of hybrid and gas cars, also reichweite2          */
    VwMessage5  reichweite2;      /* 0x6198  0x669                                                            */
    VwMessage8  hvem2;            /* 0x619E  0x5AC   high-voltage battery                                     */
    VwMessage8  gwAnt1;           /* 0x61A7  0x367   acceleration                                             */
} VwSharedMemory;

#pragma pack(pop)

_Static_assert(sizeof(VwSharedMemory) == VWSHM_SIZE, "VwSharedMemory must match the unit's layout");

#endif
