# COM Port Bridge Setup

This project bridges an external Windows application to a physical serial device while logging the traffic in the bridge console.
install: https://drive.google.com/file/d/1qrVBjmjmwEDU35nHJ8ZnZNLMLeXK0Uw3/view?usp=drive_link
create com pair COM20---COM21
                      |
                      V
                    COM5 (physical)
## Port layout

Use the ports as follows:

```text
windows application (CJ-Power chinese app)
        |
      COM20
        |
   com0com pair
        |
      COM21
        |
  com0com.exe bridge (this sw)
        |
      COM5
        |
 Physical USB serial device
```

| Port | Type | Opened by |
|---|---|---|
| `COM20` | Virtual com0com endpoint | External application |
| `COM21` | Virtual com0com endpoint | This .NET bridge |
| `COM5` | Physical hardware port | This .NET bridge |

Only one program should open each port. The external application must not open `COM5` or `COM21`.

## 1. Identify the hardware port

1. Connect the physical USB-to-serial adapter.
2. Open Windows **Device Manager**.
3. Expand **Ports (COM & LPT)**.
4. Confirm that the physical device is assigned to `COM5`.

If Windows assigns a different port, either change its port number to `COM5` in Device Manager or update the `physical` port name in `Program.cs`.

## 2. Create the com0com virtual pair

1. Install com0com using an appropriately signed build for the installed Windows version.
2. Run the com0com setup utility as Administrator.
3. Create one connected virtual port pair.
4. Assign the first endpoint the port name `COM20`.
5. Assign the second endpoint the port name `COM21`.
6. Apply/save the configuration.
7. Verify in Device Manager that both virtual ports are present without warning icons.

`COM20` and `COM21` are two ends of one virtual null-modem connection. Bytes written to one endpoint can be read from the other endpoint.

## 3. Configure the serial bridge

The bridge is configured in `Program.cs` to open:

```csharp
using (var physical = CreatePort("COM5", true))
using (var virtualPort = CreatePort("COM21", false))
```

The current serial configuration is:

```text
Baud rate:   115200
Data bits:   8
Parity:      None
Stop bits:   1
Flow control: None
Physical DTR: On
Physical RTS: Off
```

The bridge controls these settings on physical `COM5`. Serial-setting changes made by the external application on virtual `COM20` are not automatically propagated to `COM5`.

## 4. Build the bridge

Open PowerShell in the `com0com` project directory and run:

```powershell
dotnet build .\com0com.csproj --configuration Release
```

The executable is created at:

```text
bin\Release\com0com.exe
```

## 5. Start everything in the correct order

1. Close any program that has `COM5`, `COM20`, or `COM21` open.
2. Connect the physical device and confirm that it is still `COM5`.
3. Start the bridge from PowerShell:

   ```powershell
   .\bin\Release\com0com.exe
   ```

4. Leave the bridge console running.
5. Configure the external application to use `COM20` at `115200 8N1` with no flow control.
6. Start or connect the external application.

Do not configure the external application to use physical `COM5`. The bridge must have exclusive access to that port.

## 6. Observe traffic

The bridge logs transferred bytes as timestamped hexadecimal text:

```text
2026-08-19T12:00:00.0000000+05:30 APP -> DEVICE 7E 00 02
2026-08-19T12:00:00.0100000+05:30 DEVICE -> APP 7E 00 02
```

- `APP -> DEVICE` means data travelled from the external application through `COM20`/`COM21` to physical `COM5`.
- `DEVICE -> APP` means data travelled from physical `COM5` through `COM21`/`COM20` to the external application.

The bridge waits indefinitely when no bytes are being transferred. Silence after the ports have opened is normal. Press **Ctrl+C** to stop the bridge and release its ports.

## Troubleshooting

### Access to the port is denied

Another process already owns the reported port. Close serial terminals, monitoring tools, other bridge instances, and any application using that port.

### The external application cannot open COM20

- Confirm the com0com pair exists and is enabled.
- Confirm the application uses `COM20`, not `COM21`.
- Confirm no other application has `COM20` open.

### The bridge cannot open COM21

- Confirm `COM21` is the other endpoint paired with `COM20`.
- Close any serial terminal or previous bridge instance using `COM21`.

### The bridge cannot open COM5

- Confirm the hardware is connected and still assigned to `COM5`.
- Close the original external application if it is still configured to open `COM5` directly.
- Check Device Manager for driver or device errors.

### The bridge runs but shows no traffic

- Confirm the external application is connected to `COM20` and is actually transmitting.
- Confirm the physical device uses `115200 8N1` with no flow control.
- Check the physical TX, RX, and ground connections.
- Confirm that TX and RX use the correct crossing for the connected hardware.

### Important limitation

This bridge copies serial data bytes. It does not automatically mirror baud-rate, parity, stop-bit, DTR, or RTS changes requested by the external application on `COM20`. Update the physical-port configuration in `Program.cs` if the hardware requires different settings.
