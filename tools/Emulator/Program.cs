using System.IO.Ports;

if (args.Length != 1 || string.IsNullOrWhiteSpace(args[0]))
{
    Console.Error.WriteLine("Usage: Emulator <COMx>");
    Console.Error.WriteLine("Example: Emulator COM12");
    return 2;
}

string portName = args[0].Trim();
if (!portName.StartsWith("COM", StringComparison.OrdinalIgnoreCase) ||
    !int.TryParse(portName.AsSpan(3), out int portNumber) ||
    portNumber <= 0)
{
    Console.Error.WriteLine($"Invalid COM port '{portName}'. Expected a name such as COM12.");
    return 2;
}

using CancellationTokenSource stopping = new();
Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    stopping.Cancel();
};

using SerialPort port = new(portName, 115200, Parity.None, 8, StopBits.One)
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

Console.WriteLine("CJPower Emulator");
Console.WriteLine($"Port: {port.PortName} at 115200 8N1, no flow control");
Console.WriteLine("State: waiting for captured handshake; dashboard replies are gated until success");
Console.WriteLine("Press Ctrl+C to stop.");
Console.WriteLine();

port.DiscardInBuffer();
port.DiscardOutBuffer();

try
{
    new ControllerSimulator(port).Run(stopping.Token);
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

Console.WriteLine("Emulator stopped.");
return 0;
