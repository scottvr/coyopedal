#!/usr/bin/env python3
"""Dump a USB audio device's configuration descriptor to a fixture file and summarise it.

Live capture needs pyusb and libusb:

    sudo port install libusb        # or: brew install libusb
    python3 -m pip install --user pyusb
    python3 tools/usb/dump_usb_config.py --vid e5b7 --pid 0811 --out momix.bin

Offline, parse an existing fixture:

    python3 tools/usb/dump_usb_config.py --parse tests/usb/momix-cab-custom-fw-configuration.bin

The summary names every descriptor the pedal's discovery cares about: interfaces and
alternates, audio-class headers, terminals, feature units, formats, isochronous endpoints
with their sync type, and class-specific endpoint attributes (bit 0 is the sampling
frequency control). The pedal itself logs the same bytes when discovery fails, so a
maintenance-mode log is the other way to get them.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

SYNC = {0: "none", 1: "async", 2: "adaptive", 3: "sync"}
TERMINALS = {
    0x0101: "USB streaming",
    0x0201: "microphone",
    0x0301: "speaker",
    0x0302: "headphones",
    0x0601: "analog line",
    0x0603: "line connector",
    0x0710: "instrument",
}


def hexs(data: bytes) -> str:
    return " ".join(f"{b:02x}" for b in data)


def terminal_name(value: int) -> str:
    return TERMINALS.get(value, f"0x{value:04x}")


def describe(blob: bytes) -> None:
    """Walk a configuration descriptor blob and print one line per descriptor."""
    off = 0
    subclass = 0
    while off + 2 <= len(blob):
        length, kind = blob[off], blob[off + 1]
        if length < 2 or off + length > len(blob):
            print(f"  @{off:04x} malformed descriptor (length {length})")
            return
        d = blob[off : off + length]
        if kind == 2 and length >= 9:
            total, count = struct.unpack_from("<HB", d, 2)
            print(
                f"  CONFIG total={total} interfaces={count} value={d[5]} "
                f"attributes=0x{d[7]:02x} maxPower={d[8] * 2}mA"
            )
        elif kind == 4 and length >= 9:
            subclass = d[6] if d[5] == 1 else 0
            role = {1: "AudioControl", 2: "AudioStreaming", 3: "MIDIStreaming"}.get(subclass, "")
            print(
                f"  INTERFACE {d[2]} alt {d[3]} endpoints={d[4]} class={d[5]:02x}/{d[6]:02x}"
                f"/{d[7]:02x} {role}"
            )
        elif kind == 0x0B and length >= 8:
            print(f"  IAD first={d[2]} count={d[3]} class={d[4]:02x}/{d[5]:02x}/{d[6]:02x}")
        elif kind == 0x24 and subclass == 1:
            sub = d[2]
            if sub == 1:
                version = f"{d[4]}.{d[3]:02x}"
                print(f"  AC HEADER bcdADC={version} total={struct.unpack_from('<H', d, 5)[0]}")
            elif sub == 2 and length >= 12:
                print(
                    f"  INPUT TERMINAL id={d[3]} type={terminal_name(struct.unpack_from('<H', d, 4)[0])}"
                    f" channels={d[7]} config=0x{struct.unpack_from('<H', d, 8)[0]:04x}"
                )
            elif sub == 3 and length >= 9:
                print(
                    f"  OUTPUT TERMINAL id={d[3]} type={terminal_name(struct.unpack_from('<H', d, 4)[0])}"
                    f" source={d[7]}"
                )
            elif sub == 6:
                print(
                    f"  FEATURE UNIT id={d[3]} source={d[4]} controlSize={d[5]} "
                    f"controls={hexs(d[6 : length - 1])}"
                )
            elif sub == 4:
                print(f"  MIXER UNIT id={d[3]} inputs={d[4]} sources={hexs(d[5 : 5 + d[4]])}")
            elif sub == 5:
                print(f"  SELECTOR UNIT id={d[3]} inputs={d[4]} sources={hexs(d[5 : 5 + d[4]])}")
            else:
                print(f"  AC descriptor subtype={sub} {hexs(d[3:])}")
        elif kind == 0x24 and subclass == 2:
            sub = d[2]
            if sub == 1 and length >= 7:
                tag = struct.unpack_from("<H", d, 5)[0]
                print(f"  AS GENERAL terminal={d[3]} delay={d[4]} formatTag=0x{tag:04x}")
            elif sub == 2 and length >= 8:
                count = d[7]
                rates = []
                for index in range(count if count else 2):
                    base = 8 + 3 * index
                    if base + 3 <= length:
                        rates.append(d[base] | d[base + 1] << 8 | d[base + 2] << 16)
                print(
                    f"  FORMAT TYPE {d[3]} channels={d[4]} subframe={d[5]} bits={d[6]} "
                    f"rates={rates if count else f'continuous {rates}'}"
                )
            else:
                print(f"  AS descriptor subtype={sub} {hexs(d[3:])}")
        elif kind == 5 and length >= 7:
            attributes = d[3]
            size = struct.unpack_from("<H", d, 4)[0]
            extra = f" refresh={d[7]} synchAddress=0x{d[8]:02x}" if length >= 9 else ""
            iso = f" sync={SYNC[(attributes >> 2) & 3]}" if attributes & 3 == 1 else ""
            print(
                f"  ENDPOINT 0x{d[2]:02x} attributes=0x{attributes:02x}{iso} maxPacket={size} "
                f"interval={d[6]}{extra}"
            )
        elif kind == 0x25 and length >= 7:
            print(
                f"  CS ENDPOINT attributes=0x{d[3]:02x} samplingFreqControl={d[3] & 1} "
                f"lockDelayUnits={d[4]} lockDelay={struct.unpack_from('<H', d, 5)[0]}"
            )
        else:
            print(f"  descriptor type=0x{kind:02x} {hexs(d)}")
        off += length


def rebuild_from_pyusb(config) -> bytes:
    """Fallback when EP0 is not reachable: rebuild the blob from libusb's parsed copy."""
    out = bytearray(
        struct.pack(
            "<BBHBBBBB",
            config.bLength,
            config.bDescriptorType,
            config.wTotalLength,
            config.bNumInterfaces,
            config.bConfigurationValue,
            config.iConfiguration,
            config.bmAttributes,
            config.bMaxPower,
        )
    )
    out += bytes(config.extra_descriptors)
    for interface in config:
        out += struct.pack(
            "<BBBBBBBBB",
            interface.bLength,
            interface.bDescriptorType,
            interface.bInterfaceNumber,
            interface.bAlternateSetting,
            interface.bNumEndpoints,
            interface.bInterfaceClass,
            interface.bInterfaceSubClass,
            interface.bInterfaceProtocol,
            interface.iInterface,
        )
        out += bytes(interface.extra_descriptors)
        for endpoint in interface:
            fields = [
                endpoint.bLength,
                endpoint.bDescriptorType,
                endpoint.bEndpointAddress,
                endpoint.bmAttributes,
            ]
            out += struct.pack("<BBBB", *fields)
            out += struct.pack("<HB", endpoint.wMaxPacketSize, endpoint.bInterval)
            if endpoint.bLength >= 9:
                out += struct.pack("<BB", endpoint.bRefresh, endpoint.bSynchAddress)
            out += bytes(endpoint.extra_descriptors)
    return bytes(out)


def find_libusb(libusb1):
    """Locate libusb-1.0 without trusting the dynamic loader's search path.

    ctypes asks dyld, and dyld only looks in /usr/local/lib when
    DYLD_FALLBACK_LIBRARY_PATH is unset; a shell that sets it to the MacPorts
    prefix alone hides a Homebrew libusb and the other way round. So try the
    default lookup first, then every prefix a Mac or Linux box is likely to have.
    """
    candidates = [os.environ.get("LIBUSB_PATH")]
    for prefix in ("/opt/local", "/usr/local", "/opt/homebrew", "/usr"):
        for name in ("libusb-1.0.dylib", "libusb-1.0.so.0", "libusb-1.0.so"):
            candidates.append(os.path.join(prefix, "lib", name))
    backend = libusb1.get_backend()
    if backend is not None:
        return backend
    for path in candidates:
        if path and os.path.exists(path):
            backend = libusb1.get_backend(find_library=lambda _name, p=path: p)
            if backend is not None:
                return backend
    return None


def capture(vid: int, pid: int, out: str) -> int:
    try:
        import usb.core
        import usb.util
        import usb.backend.libusb1 as libusb1
    except ImportError:
        print("pyusb is not installed: python3 -m pip install --user pyusb", file=sys.stderr)
        return 2
    backend = find_libusb(libusb1)
    if backend is None:
        print(
            "libusb-1.0 was not found. Install it (sudo port install libusb, or brew install libusb)"
            " or point LIBUSB_PATH at the dylib.",
            file=sys.stderr,
        )
        return 2
    device = usb.core.find(backend=backend, idVendor=vid, idProduct=pid)
    if device is None:
        print(f"no USB device {vid:04x}:{pid:04x} is attached", file=sys.stderr)
        return 1
    print(
        f"device {device.idVendor:04x}:{device.idProduct:04x} usb={device.bcdUSB >> 8}."
        f"{device.bcdUSB & 0xFF:02x} class={device.bDeviceClass} "
        f"release={device.bcdDevice >> 8}.{device.bcdDevice & 0xFF:02x} "
        f"maxPacket0={device.bMaxPacketSize0}"
    )
    for name, index in (("manufacturer", device.iManufacturer), ("product", device.iProduct)):
        if index:
            try:
                print(f"{name}: {usb.util.get_string(device, index)}")
            except Exception as error:  # strings need EP0; the blob does not
                print(f"{name}: (unreadable: {error})")
    blob = b""
    try:
        header = bytes(usb.control.get_descriptor(device, 9, usb.util.DESC_TYPE_CONFIG, 0))
        total = struct.unpack_from("<H", header, 2)[0]
        blob = bytes(usb.control.get_descriptor(device, total, usb.util.DESC_TYPE_CONFIG, 0))
        source = "read over EP0"
    except Exception as error:
        print(f"EP0 read failed ({error}); rebuilding from libusb's parsed copy")
        blob = rebuild_from_pyusb(device.get_active_configuration())
        source = "rebuilt from libusb"
    with open(out, "wb") as handle:
        handle.write(blob)
    print(f"wrote {len(blob)} bytes ({source}) to {out}")
    describe(blob)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--vid", default="e5b7", help="vendor id, hex (default e5b7)")
    parser.add_argument("--pid", default="0811", help="product id, hex (default 0811)")
    parser.add_argument("--out", default="usb-configuration.bin", help="fixture to write")
    parser.add_argument("--parse", metavar="FILE", help="summarise an existing fixture")
    args = parser.parse_args()
    if args.parse:
        with open(args.parse, "rb") as handle:
            blob = handle.read()
        print(f"{args.parse}: {len(blob)} bytes")
        describe(blob)
        return 0
    return capture(int(args.vid, 16), int(args.pid, 16), args.out)


if __name__ == "__main__":
    sys.exit(main())
