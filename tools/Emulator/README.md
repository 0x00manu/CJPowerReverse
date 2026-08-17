# Emulator

`Emulator` is a .NET console application that behaves like the read-only
CJPower controller over a serial port. Its only argument is a conventional
Windows COM-port name.

```powershell
dotnet run --project .\tools\Emulator -- COM12
```

The other side of the virtual null-modem pair can then run `DashboardTest`:

```powershell
dotnet run --project .\tests\DashboardTest -- COM11 5
```

Behavior:

- opens the requested port at 115200 8N1 with no flow control;
- waits for all six captured `7E` handshake probes in order;
- replies to the `5A A5` identity query with `B1.T2.009-031A` and a valid CRC;
- ignores dashboard requests until the handshake succeeds;
- subsequently answers all dashboard `5A A5` register reads;
- subsequently answers the confirmed `7E` batch query;
- generates changing, correctly encoded dashboard values;
- never accepts or performs controller writes.

The protocol engine is shared at build time with
`tools/CJPowerReverse.DeviceSimulator/ControllerSimulator.cs` so both simulator
executables retain identical framing, checksums, handshake behavior, and data.
