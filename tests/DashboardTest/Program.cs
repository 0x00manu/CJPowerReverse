using System.Globalization;
using System.IO.Ports;

const int BaudRate = 115200;
TimeSpan pollPeriod = TimeSpan.FromSeconds(1);
TimeSpan responseTimeout = TimeSpan.FromMilliseconds(750);

if (!TryParseArguments(args, out string portName, out int? iterationLimit))
{
    Console.Error.WriteLine("Usage: DashboardTest <COM-port> [positive iteration count]");
    Console.Error.WriteLine("Example: DashboardTest COM5");
    return 2;
}

using CancellationTokenSource stopping = new();
Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    stopping.Cancel();
};

using SerialPort port = new(portName, BaudRate, Parity.None, 8, StopBits.One)
{
    Handshake = Handshake.None,
    DtrEnable = true,
    RtsEnable = false,
    ReadTimeout = 100,
    WriteTimeout = 1000
};

try
{
    port.Open();
}
catch (Exception exception)
{
    Console.Error.WriteLine($"Cannot open {portName}: {exception.Message}");
    return 3;
}

Console.WriteLine("CJPower live DashboardTest");
Console.WriteLine($"Port: {port.PortName} at {BaudRate} 8N1, no flow control, DTR on, RTS off");
Console.WriteLine("Mode: read-only protocol detection and monitoring. Press Ctrl+C to stop.");
Console.WriteLine();

Thread.Sleep(150);
port.DiscardInBuffer();
port.DiscardOutBuffer();

ControllerProtocol protocol;
try
{
    protocol = DetectProtocol(port, stopping.Token, out string identity);
    Console.WriteLine(protocol == ControllerProtocol.Register5AA5
        ? $"Detected: 5A A5 register protocol; controller identity: {identity}"
        : "Detected: 7E live batch-monitor protocol");
    Console.WriteLine();
}
catch (Exception exception) when (exception is TimeoutException or FormatException)
{
    Console.Error.WriteLine($"Protocol detection failed: {exception.Message}");
    return 6;
}

int successfulUpdates = 0;
int attempts = 0;

while (!stopping.IsCancellationRequested &&
       (!iterationLimit.HasValue || attempts < iterationLimit.Value))
{
    DateTimeOffset cycleStarted = DateTimeOffset.Now;
    attempts++;

    try
    {
        DashboardSnapshot snapshot = protocol == ControllerProtocol.Batch7E
            ? ReadBatchSnapshot(port, stopping.Token)
            : ReadRegisterSnapshot(port, stopping.Token);
        successfulUpdates++;
        WriteSnapshot(snapshot, attempts);
    }
    catch (TimeoutException exception)
    {
        Console.Error.WriteLine($"[{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss}] Update #{attempts}: {exception.Message}");
    }
    catch (FormatException exception)
    {
        Console.Error.WriteLine($"[{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss}] Update #{attempts}: invalid response: {exception.Message}");
    }
    catch (OperationCanceledException) when (stopping.IsCancellationRequested)
    {
        break;
    }
    catch (IOException exception)
    {
        Console.Error.WriteLine($"Serial I/O failed: {exception.Message}");
        return 4;
    }
    catch (InvalidOperationException exception)
    {
        Console.Error.WriteLine($"Serial port failed: {exception.Message}");
        return 4;
    }

    TimeSpan remaining = pollPeriod - (DateTimeOffset.Now - cycleStarted);
    if (remaining > TimeSpan.Zero &&
        (!iterationLimit.HasValue || attempts < iterationLimit.Value) &&
        !stopping.Token.WaitHandle.WaitOne(remaining))
    {
        // Continue with the next poll after the one-second period.
    }
}

Console.WriteLine($"Dashboard test stopped. Valid responses: {successfulUpdates}/{attempts}.");
return successfulUpdates == attempts ? 0 : 5;

static bool TryParseArguments(string[] arguments, out string portName, out int? iterationLimit)
{
    portName = string.Empty;
    iterationLimit = null;

    if (arguments.Length is < 1 or > 2 || string.IsNullOrWhiteSpace(arguments[0]))
    {
        return false;
    }

    portName = arguments[0];
    if (arguments.Length == 1)
    {
        return true;
    }

    if (!int.TryParse(arguments[1], NumberStyles.None, CultureInfo.InvariantCulture, out int count) ||
        count <= 0)
    {
        return false;
    }

    iterationLimit = count;
    return true;
}

static ControllerProtocol DetectProtocol(SerialPort port, CancellationToken cancellationToken, out string identity)
{
    identity = string.Empty;
    int[] delaysAfterProbeMs = [500, 70, 70, 70, 70, 500];
    for (int index = 0; index < LiveMonitorProtocol.VerificationProbes.Length; index++)
    {
        byte[] probe = LiveMonitorProtocol.VerificationProbes[index];
        port.Write(probe, 0, probe.Length);
        cancellationToken.WaitHandle.WaitOne(delaysAfterProbeMs[index]);
        cancellationToken.ThrowIfCancellationRequested();
    }

    port.DiscardInBuffer();
    port.Write(LiveMonitorProtocol.IdentificationRequest, 0, LiveMonitorProtocol.IdentificationRequest.Length);
    try
    {
        byte[] identification = ReadFrame(port, TimeSpan.FromMilliseconds(750), cancellationToken, 0x5A);
        identity = LiveMonitorProtocol.DecodeIdentification(identification);
        return ControllerProtocol.Register5AA5;
    }
    catch (TimeoutException)
    {
        port.DiscardInBuffer();
        port.Write(LiveMonitorProtocol.BatchReadRequest, 0, LiveMonitorProtocol.BatchReadRequest.Length);
        byte[] response = ReadFrame(port, TimeSpan.FromMilliseconds(750), cancellationToken, 0x7E);
        _ = LiveMonitorProtocol.DecodeResponse(response);
        return ControllerProtocol.Batch7E;
    }
}

static DashboardSnapshot ReadBatchSnapshot(SerialPort port, CancellationToken cancellationToken)
{
    port.Write(LiveMonitorProtocol.BatchReadRequest, 0, LiveMonitorProtocol.BatchReadRequest.Length);
    return LiveMonitorProtocol.DecodeResponse(
        ReadFrame(port, TimeSpan.FromMilliseconds(750), cancellationToken, 0x7E));
}

static DashboardSnapshot ReadRegisterSnapshot(SerialPort port, CancellationToken cancellationToken)
{
    Dictionary<ushort, byte[]> values = [];
    foreach ((ushort address, byte size) in LiveMonitorProtocol.DashboardRegisters)
    {
        byte[] request = LiveMonitorProtocol.BuildRegisterRead(address, size);
        port.Write(request, 0, request.Length);
        byte[] response = ReadFrame(port, TimeSpan.FromMilliseconds(750), cancellationToken, 0x5A);
        values[address] = LiveMonitorProtocol.DecodeRegisterResponse(response, address, size);
    }

    return LiveMonitorProtocol.DecodeRegisterSnapshot(values);
}

static byte[] ReadFrame(SerialPort port, TimeSpan timeout, CancellationToken cancellationToken, byte header)
{
    DateTime deadline = DateTime.UtcNow + timeout;
    List<byte> frame = new(LiveMonitorProtocol.ResponseSize);
    int expectedLength = 0;

    while (DateTime.UtcNow < deadline && !cancellationToken.IsCancellationRequested)
    {
        try
        {
            int value = port.ReadByte();
            if (value < 0)
            {
                continue;
            }

            byte received = (byte)value;
            if (frame.Count == 0)
            {
                if (received == header)
                {
                    frame.Add(received);
                }

                continue;
            }

            frame.Add(received);
            if (header == 0x7E && frame.Count == 5)
            {
                expectedLength = frame[4] + 9;
                if (expectedLength is < 9 or > 255)
                {
                    frame.Clear();
                    expectedLength = 0;
                }
            }

            else if (header == 0x5A && frame.Count == 2 && frame[1] != 0xA5)
            {
                frame.Clear();
            }

            if (header == 0x5A && frame.Count == 3)
            {
                expectedLength = frame[2] + 7;
            }

            if (expectedLength != 0 && frame.Count == expectedLength)
            {
                return frame.ToArray();
            }
        }
        catch (TimeoutException)
        {
            // SerialPort uses short read timeouts so Ctrl+C and the total deadline remain responsive.
        }
    }

    cancellationToken.ThrowIfCancellationRequested();
    throw new TimeoutException($"No complete response within {timeout.TotalMilliseconds:F0} ms");
}

static void WriteSnapshot(DashboardSnapshot value, int sequence)
{
    Console.WriteLine($"[{DateTimeOffset.Now:yyyy-MM-dd HH:mm:ss}] Update #{sequence}");
    Console.WriteLine($"  State       Drive={value.DriveStateRaw}  Running={value.RunningStateRaw}  Gear={value.VehicleGearRaw}  Mode={value.VehicleModeRaw}");
    Console.WriteLine($"  Motor       Speed={value.MotorSpeedRpm,5} RPM  Power={value.MotorPowerRaw,5} raw  Phase current={value.PhaseCurrentRaw,5} raw");
    Console.WriteLine($"  Controller  Voltage={value.ControllerVoltageV,5:F1} V  Current={value.DcCurrentA,5:F1} A  Fault=0x{value.FaultCode:X8}");
    Console.WriteLine($"  Inputs      Throttle={value.ThrottleVoltageV:F2} V  Hall={value.HallValue}  P1={value.P1}  P2={value.P2}");
    Console.WriteLine($"  Temperature MCU={value.McuTemperatureC,3} C  Motor={value.MotorTemperatureC,3} C");
    Console.WriteLine($"  Keys        Brake={OnOff(value.Brake)}  AntiTheft={OnOff(value.AntiTheft)}  Park={OnOff(value.Park)}  Cruise={OnOff(value.Cruise)}");
    Console.WriteLine($"              Reverse={OnOff(value.Reverse)}  SideStand={OnOff(value.SideStand)}  BOOST={OnOff(value.Boost)}  Acc={OnOff(value.AccGear)}  Dec={OnOff(value.DecGear)}");
    Console.WriteLine();
}

static string OnOff(bool value) => value ? "ON" : "off";
