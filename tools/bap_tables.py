#!/usr/bin/env python3
"""Print the BAP configuration built into VwCommunicationExe.exe: devices, CAN ids, functions.

Usage: bap_tables.py firmware/flash/VwUserShell/VwCommunicationExe.exe

BAP is Volkswagen's protocol on top of CAN. The program holds constant tables that say which
devices ("LSG") it talks to and, per function, which operations and data sizes are allowed.
This script reads them with pefile and prints the tables of docs/car/bap.md §3.1 and §3.4,
so that the document can be checked against the program:

  per device    one `lsg` row at 0x62f88, pointing to its rows in the three tables below
  per function  `function`: class, allowed opcodes, data types, retries
                `receive` and `send`: segmentation and CAN id of each direction

The structures are spelled out below (dissect.cstruct); pointers into RAM are not followed.
"""
import sys

import pefile
from dissect.cstruct import cstruct

DEVICES = 0x62F88                   # address of the first `lsg` row
DEVICE_COUNT = 6
CLASSES = ("Prop", "Cache", "Array", "Meth")

bap = cstruct("""
struct lsg {
    uint8   lsg;
    uint8   config_rows;
    uint16  unused_02;
    uint32  config;                 // -> 6 bytes: the BAP-Config of the device
    uint8   function_list[8];
    uint32  heartbeat_time;         // RAM
    uint8   tick_ms;
    uint8   heartbeat_factor;
    uint8   functions;              // number of rows in each of the three tables
    uint8   unused_17;
    uint32  receive_rows;
    uint32  function;               // -> function[functions]
    uint32  receive;                // -> receive[functions]
    uint32  pad_to_8_bytes;
    uint32  state;                  // RAM
    uint32  send_rows;
    uint32  send;                   // -> send[functions]
};

struct function {
    uint8   lsg;
    uint8   function;
    uint16  unused_02;
    uint32  kind;                   // 0 property, 1 cache (GetAll), 2 array, 3 method
    uint8   send_opcodes;           // bit n = opcode n may be sent
    uint8   receive_opcodes;        // bit n = opcode n is accepted
    uint16  unused_0a;
    uint32  receive_type;           // 0 none, 1 u8, 2 u16, 4 u32, 5 up to `size` bytes, 6 exactly `size` bytes
    uint32  receive_size;
    uint32  send_type;
    uint32  send_size;
    uint32  cache;                  // RAM; 0 = the value is not kept
    uint8   unused_20;
    uint8   retries;
    uint16  retry_time;             // in ticks
    uint32  timer;                  // RAM
    uint32  send_links;
    uint32  send_link;
    uint32  receive_links;
    uint32  receive_link;
};

struct receive {
    uint8   lsg;
    uint8   function;
    uint16  size;
    uint8   interface;
    uint8   unused_05[3];
    uint32  channels;               // not 0 = may arrive in several CAN messages
    uint32  timeout;
    uint32  ram[3];
    uint8   can_id;                 // low byte of the CAN id, car -> unit
    uint8   unused_1d[3];
    uint32  owners;
};

struct send {
    uint8   interface;
    uint8   unused_01[3];
    uint8   channels;               // not 0 = may be sent in several CAN messages
    uint8   unused_05[3];
    uint32  ram[2];
    uint8   channel;
    uint8   can_id;                 // low byte of the CAN id, unit -> car
    uint16  unused_12;
    uint32  owners;
    uint32  function;
};
""")


def opcodes(mask):
    return "".join(str(bit) for bit in range(8) if mask >> bit & 1)


def data_type(kind, size):
    return {0: "-", 1: "u8", 2: "u16", 4: "u32", 5: f"B<={size}", 6: f"B={size}"}[kind]


def main(arguments):
    if len(arguments) != 1:
        print(__doc__)
        return 2
    pe = pefile.PE(arguments[0], fast_load=True)
    image, base = pe.get_memory_mapped_image(), pe.OPTIONAL_HEADER.ImageBase

    def rows(structure, address, count):
        return structure[count](image[address - base:address - base + count * len(structure)])

    devices = rows(bap.lsg, DEVICES, DEVICE_COUNT)
    if [device.lsg for device in devices] != [0x0A, 0x11, 0x1D, 0x25, 0x2D, 0x31]:
        raise SystemExit("no BAP tables found: this is not VwCommunicationExe.exe of software 7.16.8 / 7.16.9")
    tables = [(device, rows(bap.function, device.function, device.functions),
               rows(bap.receive, device.receive, device.functions),
               rows(bap.send, device.send, device.functions)) for device in devices]

    print("LSG BAP-Config         TX id RX id Functions")
    for device, functions, receive, send in tables:
        config = image[device.config - base:device.config - base + 6].hex(" ").upper()
        print(f"{device.lsg:02X}  {config}  {0x600 | send[0].can_id:03X}   {0x600 | receive[0].can_id:03X}   "
              f"{device.functions}")

    print("\nLSG Fct Class TXop RXop  RX data  TX data  Seg  Cache Retry")
    for device, functions, receive, send in tables:
        for function, incoming, outgoing in zip(functions, receive, send):
            segmented = ("R" if incoming.channels else "-") + ("T" if outgoing.channels else "-")
            print(f"{function.lsg:02X}  {function.function:02X}  {CLASSES[function.kind]:<5} "
                  f"{opcodes(function.send_opcodes):<4} {opcodes(function.receive_opcodes):<5} "
                  f"{data_type(function.receive_type, function.receive_size):<8} "
                  f"{data_type(function.send_type, function.send_size):<8} {segmented:<4} "
                  f"{'yes' if function.cache else '-':<5} {function.retries}x{function.retry_time}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
