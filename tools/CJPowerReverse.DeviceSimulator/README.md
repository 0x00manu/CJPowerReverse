# CJPower device simulator

This .NET console program emulates the read-only controller behavior required by
`tests/DashboardTest` and the ESP8266 data monitor. It listens on one side of a
virtual null-modem pair at 115200 8N1.

The simulator:

- requires the exact six `7E` verification probes from `doc/HandShake.pcapng`;
- returns the captured identity `B1.T2.009-031A` with a valid `5A A5` CRC;
- ignores dashboard requests until that handshake succeeds;
- responds to all 25 `5A A5` dashboard register reads;
- responds to the confirmed 84-byte `7E` live batch request;
- produces slowly changing speed, voltage, current, throttle, Hall and key data;
- implements no write, commit or reset operations.

## Run with a virtual COM pair

With a numbered com0com pair such as `COM11` connected to `COM12`, start the
simulator on one endpoint:

```powershell
dotnet run --project .\tools\CJPowerReverse.DeviceSimulator -- COM12
```

In another terminal, connect DashboardTest to the other endpoint:

```powershell
dotnet run --project .\tests\DashboardTest -- COM11 5
```

Do not open the same endpoint from both programs. Substitute the actual paired
port names configured on the machine. Although com0com may expose names such as
`CNCA0`, .NET on Windows requires endpoints that resolve as conventional `COMn`
port names.
