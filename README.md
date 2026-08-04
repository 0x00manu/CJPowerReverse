# CJPowerReverse

This project collects USB captures and reverse-engineering notes for a device connected through a WCH/QinHeng CH343 USB-to-serial converter.

## Captures

### `usbInsert.pcapng`

Records insertion and enumeration of the USB-to-serial converter.

- USB device: WCH/QinHeng CH343
- USB ID: `1A86:55D3`
- USB address: 6
- USB version: 1.10
- Device revision: 4.45
- Configuration: bus-powered, maximum 138 mA
- Interface 0: CDC ACM control
- Interface 1: CDC data
- Bulk OUT endpoint: `0x02`
- Bulk IN endpoint: `0x82`
- Interrupt/status endpoint: `0x83`

The device enumerates successfully and accepts its vendor-specific initialization requests.

### `ConnctFailed.pcapng`

> The filename intentionally reflects the current workspace name; it is missing the second `e` in "Connect."

Records an unsuccessful attempt by the application to communicate with the downstream serial device.

The USB connection itself works:

- The CH343 enumerates and remains at USB address 6.
- Configuration 1 is selected.
- Control transfers complete normally.
- Host-to-device bulk transfers complete normally.
- Wireshark reports no malformed packets or USB expert errors.
- CH343 status notifications continue to arrive on endpoint `0x83`.

The failure occurs after USB transport. The host sends serial data through endpoint `0x02`, but no response is captured on bulk IN endpoint `0x82`.

## Failed connection traffic

The application opens or reconfigures the serial connection several times. It sends CH34x-family vendor requests `0xA1` and `0xA4`, followed by several apparent protocol probes.

Initial probe:

```text
7E 00 02 08 04 C0 90 00 20 00 82 FE 0D
```

Repeated request:

```text
7E 00 03 01 02 E0 C0 C0 9A FD 0D
```

Additional probe:

```text
7E 00 03 02 04 C0 30 00 20 00 E7 FE 0D
```

The application then tries a different-looking message format:

```text
5A A5 03 05 80 10 C5 2E 0D 0A
```

It finishes with repeated short polls:

```text
00 00 01
03 80 10
```

No reply to any of these messages appears in the capture.

## Current diagnosis

The evidence does not indicate a USB enumeration or basic CH343 driver failure. Likely causes are downstream of the USB converter:

1. Incorrect baud rate, parity, stop-bit, or flow-control setting.
2. Incorrect TX/RX wiring or missing common ground.
3. TTL UART connected to an incompatible RS-232/RS-485 electrical interface.
4. Downstream equipment is unpowered or not in communication mode.
5. RTS/DTR or hardware flow-control state is blocking communication.
6. Incorrect device address, command format, or protocol selection.

## Recommended next steps

1. Capture a known successful connection and compare it with `ConnctFailed.pcapng`.
2. Record the exact COM-port configuration used for each attempt.
3. Check TX, RX, ground, and voltage levels.
4. Observe the UART pins with a logic analyzer or oscilloscope to confirm that bytes leave the CH343 and to measure the baud rate.
5. Capture one controlled experiment per baud rate or protocol change.
6. Use recognizable test payloads so request and response boundaries are unambiguous.

## Analysis tools

Wireshark 4.6.7, TShark, Npcap, and USBPcap are installed. Useful commands include:

```powershell
capinfos .\ConnctFailed.pcapng
tshark -r .\ConnctFailed.pcapng
tshark -r .\ConnctFailed.pcapng -V
tshark -r .\ConnctFailed.pcapng -T json
tshark -r .\ConnctFailed.pcapng -q -z expert
```

Original capture files should be treated as immutable evidence.

## Successful read/write session

`conReadWrite.pcapng` records a successful application session, while `conReadWrite.dts` contains the controller settings read through the USB-to-UART connection.

Capture summary:

- 5,501 USB packets
- Duration: approximately 129 seconds
- CH343 USB address in this capture: 4
- 1,350 valid application requests
- 1,350 matching application responses
- No checksum failures in the 2,700 decoded application frames

USB addresses are assigned dynamically. Address 4 in this capture does not conflict with address 6 in the earlier captures.

## Confirmed serial protocol

Application messages use this structure:

```text
5A A5 LEN ADDR_LO ADDR_HI BODY... CRC_LO CRC_HI 0D 0A
```

Fields:

- `5A A5` - frame header
- `LEN` - number of bytes from `ADDR_LO` through the end of `BODY`
- `ADDR_LO ADDR_HI` - little-endian address with operation flags in the upper bits
- `BODY` - read length, returned data, or data to write
- `CRC_LO CRC_HI` - little-endian CRC
- `0D 0A` - frame terminator

### Checksum

The checksum is CRC-16/CCITT-FALSE:

```text
Polynomial:    0x1021
Initial value: 0xFFFF
Input:         ADDR_LO, ADDR_HI, and BODY bytes
Output order:  CRC low byte followed by CRC high byte
```

The header, `LEN`, and `0D 0A` terminator are not included in the CRC calculation.

All 2,700 decoded request and response frames in `conReadWrite.pcapng` pass this checksum rule.

## Read operations

A read request uses:

```text
encoded_address = 0x8000 | parameter_address
BODY = number of bytes to read
```

For example, decimal address 1132 is `0x046C`. Requesting two bytes produces:

```text
5A A5 03 6C 84 02 88 D5 0D 0A
         └─────┘ └─ read 2 bytes
          0x846C = 0x8000 | 0x046C
```

The response echoes the encoded address and replaces the requested-length byte with the returned data:

```text
5A A5 04 6C 84 00 00 BA 75 0D 0A
                  └───┘ returned value
```

The response length matches the byte count requested by the application.

## Write operations

A write request uses:

```text
encoded_address = 0x2000 | parameter_address
BODY = value encoded in little-endian form
```

For example, `MaxSpeed` is stored at decimal address 534 (`0x0216`). Writing 3300 (`0x0CE4`) produces a frame containing:

```text
16 22 E4 0C
└───┘ └───┘
0x2216  3300
```

Here, `0x2216 = 0x2000 | 0x0216`.

The controller acknowledges each write by echoing the encoded address and returning the number of bytes written as a 16-bit little-endian value:

```text
01 00    one byte written
02 00    two bytes written
04 00    four bytes written
```

The capture contains 119 write operations covering 116 unique `.dts` addresses. Every write maps to an entry in `conReadWrite.dts`.

## DTS parameter mapping

The `adress` property in `conReadWrite.dts` is the controller parameter address. The spelling `adress` is retained because that is how it appears in the source file.

The `adrSize` property determines the raw representation:

```text
u8, i8      1 byte
u16, i16    2 bytes, little-endian
u32, i32    4 bytes, little-endian
```

Signed types use two's-complement representation.

The observed conversion between the displayed value and stored value is:

```text
raw_value = displayed_value × ratio_K + ratio_B
```

Examples:

| Parameter | Displayed value | Conversion | Raw value |
|---|---:|---:|---:|
| `MaxSpeed` | 3300 | `3300 × 1` | `3300` / `0x0CE4` |
| `MotorTempWarn` | 110 | `110 × 1 + 40` | `150` / `0x96` |
| `AccRatio` | 5 | `5 × 1000` | `5000` / `0x00001388` |
| `RevCurPercent` | 100 | `100 × -10` | `-1000` / `0xFC18` |
| `OverVolt` | 4.9 | `4.9 × 744.73` | approximately `3649` / `0x0E41` |

Conversion results involving non-integer ratios should be rounded according to behavior confirmed from additional controlled captures.

### Disabled parameters

The only `.dts` addresses not written during the captured write operation are the five entries marked `"disable": true`:

| Address | Parameter |
|---:|---|
| 1054 | `VectorDir` |
| 1053 | `MotorDir` |
| 930 | `PhaseCurClai1` |
| 932 | `PhaseCurClai2` |
| 934 | `DcCurClai` |

The `.dts` file also contains repeated UI entries for addresses 1062, 1030, and 1032. These repeated entries correspond to repeated writes of the same controller parameters.

## Remaining unknowns

- The exact meanings of two final command frames using base addresses 0 and 2 remain unconfirmed.
- The unmapped low addresses repeatedly read during the session likely contain live status or telemetry, but their field meanings require further correlation.
- The precise rounding and overflow behavior for every `ratio_K` conversion needs controlled testing.
- Write safety constraints, commit behavior, and whether a reset is required after some changes remain to be established.

Do not write undocumented or out-of-range values to a live motor controller. Reproduce read-only operations first and validate all generated CRCs before implementing write support.

## .NET protocol library

The solution [CJPowerReverse.slnx](CJPowerReverse.slnx) contains:

- `src/CJPowerReverse.Protocol` - dependency-free .NET 10 class library
- `tests/CJPowerReverse.Protocol.Tests` - offline executable verification suite using captured frames

Build and verify it with:

```powershell
dotnet build .\CJPowerReverse.slnx --configuration Release
dotnet run --project .\tests\CJPowerReverse.Protocol.Tests --configuration Release
```

Create a read request:

```csharp
using CJPowerReverse.Protocol;

ProtocolFrame request = ProtocolMessages.CreateReadRequest(
    address: 1132,
    byteCount: 2);

byte[] serialBytes = request.Encode();
```

Create a typed write request:

```csharp
long raw = DtsValueConverter.ToRaw(
    displayedValue: 3300,
    ratioK: 1,
    ratioB: 0);

byte[] value = ParameterValueCodec.Encode(raw, ParameterDataType.U16);
ProtocolFrame request = ProtocolMessages.CreateWriteRequest(534, value);
byte[] serialBytes = request.Encode();
```

Decode and validate a received frame:

```csharp
if (ProtocolFrameCodec.TryDecode(serialBytes, out ProtocolFrame? frame, out ProtocolDecodeError error))
{
    Console.WriteLine($"{frame.Operation} address {frame.Address}");
}
```

The decoder validates the header, declared length, terminator, and CRC before returning a frame. Serial-port discovery, connection management, retries, timeouts, and live writes are intentionally outside the class library at this stage.

## Hardware read test

`tools/CJPowerReverse.HWTest` is a read-only console program for testing the reconstructed protocol against a real controller.

Run it from the repository root and pass the Windows COM-port name as its only argument:

```powershell
dotnet run --project .\tools\CJPowerReverse.HWTest -- COM5
```

The program opens the port using the settings recovered from the successful capture:

```text
Baud rate:    115200
Data bits:    8
Parity:       None
Stop bits:    1
Flow control: None
DTR:          Enabled
RTS:          Disabled
```

It loads `conReadWrite.dts`, reads every unique parameter address using the size declared by `adrSize`, validates each response and CRC, applies `ratio_K` and `ratio_B`, and displays the raw and scaled values. Picker values include their text label when the returned index is known.

Each address is attempted up to three times with a 750 ms response timeout. Duplicate DTS entries reuse the result from their shared address. A failed parameter is reported without stopping the remaining reads.

Safety boundary: HWTest only constructs `0x8000 | address` read requests. It does not send write frames, commit commands, reset commands, or the two unconfirmed final commands seen in the Windows application capture.

Typical output format:

```text
Port: COM5 at 115200 8N1, flow control none, DTR on, RTS off
Mode: read-only; no write or commit commands will be sent

Group 1
----------------------------------------------------------------------------------------------------
  534  MaxSpeed(rpm):                   U16 raw=        3300  value=        3300
```

Exit codes:

| Code | Meaning |
|---:|---|
| 0 | Every unique address was read successfully |
| 2 | Missing or invalid command-line arguments |
| 3 | DTS file could not be loaded |
| 4 | COM port could not be opened |
| 5 | One or more parameter reads failed |

## Live data-monitor protocol

`doc/screen_shot_live.pcapng` records the Windows application's continuously updating Data Monitor screen. The related artifacts are:

- `doc/DataMonitor.cs` - approximate C# view model
- `doc/DataMonitor.jpg` - example UI
- `doc/DataMonitor.json` - incomplete placeholder JSON

The capture contains 184 polling requests and 184 matching responses over approximately 25.6 seconds, giving an update rate of about 7.2 polls per second. The CH343 has USB address 4 in this capture.

This monitor uses the older `7E` batch-read protocol, not the `5A A5` single-register protocol used by `conReadWrite.pcapng`.

### Batch-read request

Every request is 84 bytes:

```text
7E 00 02 02 4B 10
[25 address-low, address-high, size descriptors]
CHECKSUM_LO CHECKSUM_HI
0D
```

The 25 descriptors request a total of 36 data bytes. Each descriptor contains a 16-bit little-endian address followed by a one-byte size.

### Batch-read response

Every response is 45 bytes:

```text
7E 00 02 82 24 A0
[36 returned data bytes]
CHECKSUM_LO CHECKSUM_HI
0D
```

Returned values appear in the same order and with the same sizes as the request descriptors.

### Batch checksum

The `7E` protocol does not use CRC-16/CCITT-FALSE. Its 16-bit checksum is the two's complement of the byte sum:

```text
checksum = -sum(bytes from frame index 1 through the final data byte) mod 65536
```

The checksum is transmitted little-endian. The leading `7E`, checksum bytes, and trailing `0D` are excluded from the sum.

### Live field mapping

| Address | Size | Data Monitor field | Confirmed or likely conversion |
|---:|---:|---|---|
| 57 | 1 | `driveState` | State/flags; values 0, 2 and 4 observed |
| 245 | 1 | `runningState` | Raw state value |
| 192 | 1 | `vehicleGear` | Raw state value |
| 203 | 1 | `vehicleMode` | Raw state value |
| 105 | 2 | `MotorSpd` | Signed little-endian RPM |
| 239 | 4 | `FaultCode` | 32-bit fault code or bitmap |
| 119 | 2 | `DcCurrent` | Signed raw value divided by 10 A |
| 113 | 2 | `DcVoltage` | Unsigned raw value divided by 10 V |
| 224 | 2 | `PhaseCurrent` | Signed; exact scaling remains unconfirmed |
| 115 | 2 | `MotorPower` | Signed; exact kW scaling remains unconfirmed |
| 220 | 2 | `ThrottleVoltage` | Raw value divided by approximately 744.73 V |
| 222 | 1 | `MCUTemperature` | Raw value minus 40 degrees C |
| 223 | 1 | `MotorTemperature` | Raw value minus 40 degrees C |
| 90 | 1 | `HallValue` | Raw Hall state |
| 1130 | 2 | `P1` | Unsigned little-endian value |
| 1132 | 2 | `P2` | Unsigned little-endian value |
| 232 | 1 | `Brake` | Boolean/function state |
| 233 | 1 | `AntiTheft` | Boolean/function state |
| 234 | 1 | `Park` | Boolean/function state |
| 235 | 1 | `Cruise` | Boolean/function state |
| 236 | 1 | `Reverse` | Boolean/function state |
| 237 | 1 | `SideStand` | Boolean/function state |
| 238 | 1 | `BOOST` | Boolean/function state |
| 243 | 1 | `AccGear` | Boolean/function state |
| 244 | 1 | `DecGear` | Boolean/function state |

Examples that directly match the displayed units include:

```text
FF FE = -2  -> -0.2 A DC current
01 E1 = 481 -> 48.1 V DC voltage
02 3A = 570 -> approximately 0.8 V throttle input
4B = 75     -> 35 degrees C MCU temperature
05 02       -> P1 value 1282
```

### DataMonitor model limitations

- `DriveState.Read` is probably a misspelling of `Ready`.
- Address 57 produces raw values 0, 2 and 4, so the existing `DriveState` enum is incomplete or the byte is a bit field.
- `FaultCode` is declared as `short`, but the live request and response allocate four bytes; it should likely be `uint` or `int`.
- Current, voltage, throttle and temperature properties require scaling rather than direct `short` display.
- The screenshot is not an exact frame from this capture: its displayed state combination and motor temperature do not occur exactly in the recorded responses.
- `DataMonitor.json` currently contains null placeholders and does not describe addresses, sizes, scaling or live values.
