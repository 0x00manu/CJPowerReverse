# CJPowerReverse Agent Notes

## Project purpose

This workspace is used to reverse-engineer and develop software for a USB-connected device. Preserve original captures and other evidence; create derived notes, scripts, and decoded artifacts as separate files.

## Current workspace

- `usbInsert.pcapng` - USBPcap capture made while a USB device was inserted on Windows.
- The capture was produced by Dumpcap/Wireshark 4.6.7 on 64-bit Windows 10 22H2 (build 19045), using the `USBPcap3` interface.
- The original capture is small: 34 enhanced packet blocks and approximately 4.3 KB.

## Confirmed target device

The newly inserted target begins enumerating at USB address 6.

- Vendor ID: `0x1A86` (WCH/QinHeng)
- Product ID: `0x55D3`
- Likely chip: WCH CH343 USB-to-serial converter
- USB specification: 1.10
- Device revision (`bcdDevice`): 4.45
- EP0 maximum packet size: 8 bytes
- Configuration count: 1
- Power: bus-powered, maximum 138 mA

Device descriptor bytes:

```text
12 01 10 01 02 00 00 08 86 1A D3 55 45 04 00 02 03 01
```

String descriptors were not captured, so the product string and serial number are currently unknown.

## Configuration and interfaces

Configuration 1 has two interfaces and a total descriptor length of 67 bytes.

### Interface 0 - CDC ACM control

- Class: `0x02` (Communications)
- Subclass: `0x02` (Abstract Control Model)
- Protocol: `0x01`
- Endpoint `0x83`: interrupt IN, maximum packet size 16, interval 1
- CDC header version: 1.10
- CDC ACM capability byte: `0x02`
- CDC Union links control interface 0 to data interface 1

### Interface 1 - CDC data

- Class: `0x0A` (CDC Data)
- Endpoint `0x02`: bulk OUT, maximum packet size 32
- Endpoint `0x82`: bulk IN, maximum packet size 64

## Observed insertion sequence

The target enumeration starts around `2026-07-31 12:28:54.810 UTC` and the visible sequence lasts approximately 1.7 ms.

1. Read the 18-byte device descriptor.
2. Send vendor request `0x5F` and receive `47 08` (only two response bytes were captured despite a larger requested length).
3. Read the 9-byte configuration header.
4. Read the full 67-byte configuration descriptor.
5. Select configuration 1.
6. Send vendor initialization request:
   - `bmRequestType = 0x40`
   - `bRequest = 0xA1`
   - `wValue = 0xC39C`
   - `wIndex = 0xD902`
   - no data stage
7. Send vendor initialization request:
   - `bmRequestType = 0x40`
   - `bRequest = 0xA4`
   - `wValue = 0x00FF`
   - `wIndex = 0x0000`
   - no data stage

No failed transfers, malformed descriptors, line-coding request, modem-control request, or serial application payload are visible in this capture.

## Unrelated devices in the capture

Earlier packets contain descriptor traffic for devices at addresses 2, 3, and 4. These appear to be devices already present on the bus and are not the insertion target. Known IDs visible in those descriptors include:

- `138A:003F`
- `05C8:0369` (USB video-class device)
- `0A5C:21F1` (Broadcom composite device)

Do not mix their descriptors or traffic into the CH343 analysis.

## `ConnctFailed.pcapng` analysis

The workspace filename is `ConnctFailed.pcapng` (missing the second `e` in "Connect"). Its SHA-256 at analysis time is:

```text
d07428ed413790d541ef783064189c9b3becb2f2ff071b12926d25c075c69d83
```

Capture metadata:

- 122 packets
- 9,856-byte pcapng file
- 5,558 bytes of captured packet data
- Duration: 13.673333 seconds
- Earliest timestamp: 2026-07-31 18:22:26.333814 local capture display time
- Encapsulation: USB packets with USBPcap header
- Interface: `\\.\USBPcap3`

### Principal conclusion

The failed connection is not a USB enumeration failure. The CH343 at address 6 is configured successfully, accepts control transfers and bulk-OUT data, and continues producing interrupt/status reports. The host submits bulk-IN URBs on endpoint `0x82`, but no bulk-IN completion carrying serial data occurs during the capture. The evidence therefore places the failure at the UART wiring/configuration or application-protocol layer downstream of the USB converter.

TShark produced no malformed-packet or USB expert warnings. Do not describe the capture as proving that the downstream equipment received the bytes: USB bulk-OUT completion proves delivery to the CH343/USB stack, not delivery over the physical UART.

### Connection and reconfiguration timeline

- Frames 1-24: descriptor/configuration traffic for devices already present, including CH343 address 6.
- Frames 25-53, around 4.228 seconds: first CH343 serial initialization and status activity.
- Frames 54-81, around 5.993-6.797 seconds: reconfiguration and first application protocol probes.
- Frames 82-101, around 7.331-7.343 seconds: another reconfiguration and a different-looking protocol probe.
- Frames 102-119, around 8.352-12.635 seconds: repeated three-byte polling attempts.
- Frames 120-122, around 13.673 seconds: final modem-control/status activity.

### CH34x-family vendor requests

The following requests occur repeatedly:

- Request `0xA1`, `wValue = 0xC39C`, with observed `wIndex` values `0xD902`, `0x9803`, and `0xCC03`.
- Request `0xA4`, with observed `wValue` values `0x00FF`, `0x00DF`, and `0x009F`.

Open-source CH341-family drivers name `0xA1` as serial initialization and `0xA4` as modem control. Treat the exact CH343 parameter and bit meanings as unconfirmed until correlated with the WCH driver, authoritative documentation, or controlled setting changes.

### Interrupt/status endpoint data

Endpoint `0x83` returns seven-byte status payloads, primarily:

```text
08 FF FF B7 00 80 0F
08 DF BF B7 00 80 0F
```

These reports show continuing USB communication and changing status bits. Their detailed proprietary semantics remain unconfirmed.

### Bulk-OUT application payloads

Frames 70-80 contain:

```text
Frame 70: 7E 00 02 08 04 C0 90 00 20 00 82 FE 0D
Frame 72: 7E 00 03 01 02 E0 C0 C0 9A FD 0D
Frame 74: 7E 00 03 01 02 E0 C0 C0 9A FD 0D
Frame 76: 7E 00 03 01 02 E0 C0 C0 9A FD 0D
Frame 78: 7E 00 03 01 02 E0 C0 C0 9A FD 0D
Frame 80: 7E 00 03 02 04 C0 30 00 20 00 E7 FE 0D
```

Frame 100 contains a different message format:

```text
5A A5 03 05 80 10 C5 2E 0D 0A
```

Frames 102-118 contain repeated three-byte messages:

```text
Frames 102, 104, 106, 108: 00 00 01
Frames 110, 112, 114, 116, 118: 03 80 10
```

There is no response data on bulk-IN endpoint `0x82`. Frames 34-43 are bulk-IN submissions with zero captured data and no corresponding data-bearing completion in this capture.

### Failure hypotheses to test

Test these independently rather than treating any one as established:

1. Wrong baud rate, parity, stop bits, or flow control.
2. TX/RX incorrectly crossed or not crossed, or missing common ground.
3. TTL/CMOS UART versus RS-232 or RS-485 electrical-level mismatch.
4. Downstream equipment unpowered, disabled, or in the wrong operating mode.
5. RTS/DTR/modem-control state preventing communication.
6. Incorrect application protocol, target address, framing, or checksum.

The most valuable next evidence is a known-good capture or a simultaneous UART logic-analyzer trace correlated with USBPcap timestamps.

## Development guidance

- Treat VID/PID and raw descriptor values as confirmed evidence; treat the CH343 model name as an identification based on public USB-ID mappings.
- Keep raw evidence immutable. Do not modify or overwrite `.pcap` or `.pcapng` files.
- When adding parsers, retain raw byte order and explicitly document whether multibyte USB fields are little-endian.
- Distinguish USBPcap submission and completion records before interpreting nonzero status-looking fields as failures.
- Correlate control-transfer submissions and completions using the USBPcap IRP identifier.
- Prefer checked-in, reproducible parsing scripts over manual-only Wireshark observations.
- Record unknown vendor requests without assigning semantics until confirmed from additional captures, a driver, or authoritative documentation.
- Avoid assuming this CDC-like device uses only standard CDC ACM requests; the capture proves vendor-specific initialization is involved.

## Recommended next captures

For useful protocol reconstruction, collect separate captures for:

1. Insertion through completion of driver initialization, including string descriptors.
2. Opening the COM port at a known configuration such as 9600 8N1.
3. Changing baud rate, parity, stop bits, flow control, DTR, and RTS one setting at a time.
4. Transmitting recognizable byte patterns in both directions.
5. Closing and reopening the port.
6. Unplugging the device while idle and while traffic is active.

Document the exact user action and local time for every capture. Use distinctive payloads such as incrementing bytes or short ASCII markers so transfer boundaries can be identified reliably.

## Installed capture-analysis tools

Wireshark 4.6.7 is installed in `C:\Program Files\Wireshark`. The directory is also included in the user's `PATH` (new processes may be required to observe the PATH change).

Verified executables include:

- `tshark.exe` - command-line packet dissection and structured field export
- `capinfos.exe` - capture metadata, timing, packet counts, and hashes
- `dumpcap.exe` - packet capture engine
- `editcap.exe` - capture conversion, slicing, and deduplication
- `mergecap.exe` - capture merging
- `rawshark.exe` - raw packet dissection
- `sharkd.exe` - JSON-RPC Wireshark dissection service
- `USBPcapCMD.exe` in `C:\Program Files\USBPcap` - Windows USB capture

Npcap 1.88 and USBPcap are installed. TShark can see USB capture interfaces `\\.\USBPcap1`, `\\.\USBPcap2`, and `\\.\USBPcap3`.

Useful commands:

```powershell
tshark --version
tshark -D
capinfos .\usbInsert.pcapng
tshark -r .\usbInsert.pcapng
tshark -r .\usbInsert.pcapng -V
tshark -r .\usbInsert.pcapng -T json
tshark -r .\usbInsert.pcapng -Y "usb"
```

The installation was validated by reading `usbInsert.pcapng`: TShark recognized all 34 frames as USB/USBPcap traffic. Scapy, dpkt, and pyshark are not installed and are not currently required because TShark provides the authoritative Wireshark dissectors and machine-readable output.
