# ESP8266 Data Monitor

Read-only ESP8266 firmware for the CJ Power motor-controller live-monitor protocol.

## Wiring

| ESP8266 | Motor controller |
|---|---|
| GPIO1 / UART0 TX | RX |
| GPIO3 / UART0 RX | TX |
| GND | GND |

UART0 uses 115200 baud, 8 data bits, no parity, one stop bit, and no flow control. UART1 TX on GPIO2 is used only for diagnostics.

ESP8266 GPIO is 3.3 V only. Verify the controller voltage before connecting it. UART0 emits bootloader messages during reset; isolate TX if unsolicited startup bytes could be unsafe.

## Build

```powershell
python -m platformio run -d .\ESP8266-data-monitor
```

Run native protocol tests:

```powershell
python -m platformio test -d .\ESP8266-data-monitor -e native
```

The native test environment requires GCC or Clang on `PATH`. The tests are included but cannot execute on the current development machine until a native C/C++ compiler is installed. This does not affect compilation of the ESP8266 target, which uses PlatformIO's Xtensa toolchain.

Upload after selecting the correct serial port:

```powershell
python -m platformio run -d .\ESP8266-data-monitor -t upload --upload-port COM5
```

## Dashboard

1. Connect to WPA2 access point `CJPowerMonitor`.
2. Use password `CJPower123`.
3. Open `http://192.168.4.1/`.

The firmware polls the captured 25-field batch request every 150 ms. It validates the response checksum, maintains communication statistics, and exposes `/api/live` and `/api/status` JSON endpoints.

At startup it first reproduces the read-only protocol-verification sequence from
`doc/HandShake.pcapng`: six captured `7E` probes followed by the `5A A5` identity
read for address 5. A valid identity (captured example `B1.T2.009-031A`) is shown
in `/api/status` and on diagnostic UART1. The firmware then begins the confirmed
`7E` live batch polling. No write, commit, or reset command is sent.

The identity response has `LEN=0x10`: this counts the two encoded-address bytes
plus the 14-character identity, rather than returning 16 identity bytes.

The firmware is deliberately read-only. It contains no parameter-write, commit, reset, or undocumented command implementation.
