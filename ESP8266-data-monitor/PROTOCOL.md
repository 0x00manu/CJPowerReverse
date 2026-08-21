# Controller serial protocols

This document describes the two application protocols observed between the
Windows program and the controller. Both run over the CH343 serial connection
using **8 data bits, no parity, 1 stop bit, and no flow control**. The baud rate
depends on the application protocol: `7E` uses **57600 baud**, while `5A A5`
uses **115200 baud**.
All hex listings contain bytes, not ASCII text: a terminal must transmit `7E`
as the single byte `0x7E`, rather than the two characters `7` and `E`.

The `7E` protocol can perform a live batch read for the Data Monitor screen. The
successful connection trace uses `5A A5` individual register reads after the
identity handshake. The protocols have different framing and checksums.

> **Safety:** Start with read requests only. Do not send `5A A5` write frames,
> commit/reset commands, or experimental commands to valuable hardware. Confirm
> that the UART electrical level is 3.3 V compatible and connect a common ground.

## Sending hex from a serial terminal

Configure the port as `57600 8N1` for `7E`, or `115200 8N1` for `5A A5`, with
flow control `None`, then use a terminal's binary/hex-send function. Do not enable
automatic CR/LF: the required terminator bytes are already present in each
example. Clear old receive data before sending one request and display received
data as hexadecimal.

Not every text-oriented terminal can transmit arbitrary binary bytes. RealTerm's
**Send Numbers** feature or a small script that converts the listings below to
bytes is suitable. Tera Term's normal text entry is not a raw-hex sender.

## `7E` live batch-monitor protocol

### Confirmed request framing

The application repeatedly sent this 84-byte read request:

```text
7E 00 02 02 4B 10
39 00 01 F5 00 01 C0 00 01 CB 00 01 69 00 02
EF 00 04 77 00 02 71 00 02 E0 00 02 73 00 02
DC 00 02 DE 00 01 DF 00 01 5A 00 01 6A 04 02
6C 04 02 E8 00 01 E9 00 01 EA 00 01 EB 00 01
EC 00 01 ED 00 01 EE 00 01 F3 00 01 F4 00 01
0C ED 0D
```

Its layout is:

```text
7E 00 02 02 LL 10 [LL bytes of descriptors] CHECKSUM_LO CHECKSUM_HI 0D
```

- `LL = 0x4B` is the 75-byte descriptor-block length.
- Each three-byte descriptor is `ADDRESS_LO ADDRESS_HI SIZE`.
- Addresses are unflagged 16-bit little-endian values.
- The 25 descriptors above request 36 returned data bytes.
- Only this exact request was captured and is used by the ESP8266 firmware.

### Confirmed response framing

```text
7E 00 02 82 24 A0 [36 data bytes] CHECKSUM_LO CHECKSUM_HI 0D
```

`0x24` is the 36-byte returned-data length. Values are concatenated in request
descriptor order; there are no addresses in the response. A response therefore
has meaning only when paired with the request that produced it.

### `7E` checksum

For requests and responses:

```text
checksum = (-sum(frame[1] through final payload byte)) & 0xFFFF
```

Transmit the 16-bit result little-endian. Exclude the leading `7E`, checksum
bytes, and final `0D`. The full request above has checksum `ED0C`, transmitted as
`0C ED`.

### Live field order

Offsets are relative to the response data beginning immediately after `A0`.

| Offset | Address | Size | Field | Conversion |
|---:|---:|---:|---|---|
| 0 | 57 | 1 | Drive state | Raw flags/state |
| 1 | 245 | 1 | Running state | Raw state |
| 2 | 192 | 1 | Vehicle gear | Raw state |
| 3 | 203 | 1 | Vehicle mode | Raw state |
| 4 | 105 | 2 | Motor speed | Signed LE, RPM |
| 6 | 239 | 4 | Fault code | Unsigned LE bitmap |
| 10 | 119 | 2 | DC current | Signed LE / 10 A |
| 12 | 113 | 2 | Controller/DC voltage | Unsigned LE / 10 V |
| 14 | 224 | 2 | Phase current | Signed LE; scale unconfirmed |
| 16 | 115 | 2 | Motor power | Signed LE; scale unconfirmed |
| 18 | 220 | 2 | Throttle voltage | Unsigned LE / 744.73 V |
| 20 | 222 | 1 | MCU temperature | Raw - 40 degrees C |
| 21 | 223 | 1 | Motor temperature | Raw - 40 degrees C |
| 22 | 90 | 1 | Hall value | Raw state |
| 23 | 1130 | 2 | P1 | Unsigned LE |
| 25 | 1132 | 2 | P2 | Unsigned LE |
| 27-35 | 232-238, 243-244 | 1 each | Function keys | Raw Boolean/state |

To obtain controller voltage using the **confirmed `7E` exchange**, transmit the
complete 84-byte request above. Read response data bytes at offsets 12 and 13 as
an unsigned little-endian integer and divide by 10. For example, bytes `E1 01`
give `0x01E1 = 481`, or **48.1 V**.

A shorter, one-descriptor `7E` request can be inferred from the framing, but it
was not observed in the capture and controller support has not been established.
The firmware deliberately uses the confirmed full batch request instead.

## `5A A5` single-register protocol

`CJPower-connect-pass.sal` confirms this successful sequence: six `7E` probes at
57600, a `5A A5` identity request at 115200, then continued `5A A5` dashboard
reads at 115200. The application does not return to 57600 in that workflow.

### Frame format

```text
5A A5 LEN ADDR_LO ADDR_HI BODY... CRC_LO CRC_HI 0D 0A
```

- `LEN` counts `ADDR_LO`, `ADDR_HI`, and all body bytes.
- The address is little-endian and includes operation bits.
- A read address is `0x8000 | parameter_address`.
- A write address is `0x2000 | parameter_address`.
- A read-request body contains the number of bytes requested.
- A read-response body contains the returned bytes.

### CRC

`5A A5` frames use CRC-16/CCITT-FALSE:

```text
Polynomial:       0x1021
Initial value:    0xFFFF
Reflected:        no
Final XOR:        0x0000
CRC input:        ADDR_LO, ADDR_HI, BODY...
Transmission:     CRC_LO, CRC_HI
```

The header, `LEN`, CRC itself, and `0D 0A` are excluded from the calculation.

### Read controller/DC voltage

Controller/DC voltage is address 113 (`0x0071`) and occupies two bytes. Its read
address is `0x8071`. Send this exact ten-byte frame:

```text
5A A5 03 71 80 02 7E 18 0D 0A
```

Field breakdown:

```text
5A A5 | 03 | 71 80 | 02 | 7E 18 | 0D 0A
header   len  0x8071  2B   CRC      end
```

A response returning raw voltage 481 would be:

```text
5A A5 04 71 80 E1 01 F9 B8 0D 0A
```

Decode `E1 01` as little-endian `0x01E1 = 481`, then divide by 10 to obtain
**48.1 V**. This response is an illustrative correctly checksummed frame; the
actual bytes depend on the live controller voltage.

Other useful read-only request examples are:

| Desired value | Address | Request hex | Decode |
|---|---:|---|---|
| Motor speed | 105 | `5A A5 03 69 80 02 BC F2 0D 0A` | signed LE, RPM |
| DC current | 119 | `5A A5 03 77 80 02 DE AA 0D 0A` | signed LE / 10 A |
| MCU temperature | 222 | `5A A5 03 DE 80 01 B0 B9 0D 0A` | byte - 40 degrees C |
| Motor temperature | 223 | `5A A5 03 DF 80 01 80 8E 0D 0A` | byte - 40 degrees C |

These addresses and conversions are confirmed by their use in the captured live
monitor mapping. The example request frames are generated with the confirmed
`5A A5` read framing and CRC. Hardware behavior should still be verified because
the Windows captures used the `7E` protocol for the live Data Monitor screen.

## Evidence boundaries

- The two protocols were observed in different application workflows.
- `7E` arbitrary descriptor lists are plausible, but only the fixed 25-field
  request and its matching response layout are capture-confirmed.
- `5A A5` individual reads are confirmed in `conReadWrite.pcapng`; the live-field
  association comes from the `7E` Data Monitor capture.
- USB bulk transfer success does not prove that bytes reached the controller's
  UART pins or that the controller accepted them.
- Preserve raw captures and record any hardware response before extending these
  conclusions.
