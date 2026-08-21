using System.IO.Ports;

if (args.Length != 1 || string.IsNullOrWhiteSpace(args[0]))
{
    Console.Error.WriteLine("Usage: CJPowerReverse.DeviceSimulator <COM-port>");
    Console.Error.WriteLine("Example: CJPowerReverse.DeviceSimulator CNCB0");
    return 2;
}

using CancellationTokenSource stopping = new();
Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    stopping.Cancel();
};

using SerialPort port = new(args[0], 57600, Parity.None, 8, StopBits.One)
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
    Console.Error.WriteLine($"Cannot open {args[0]}: {exception.Message}");
    return 3;
}

Console.WriteLine("CJPower controller simulator");
Console.WriteLine($"Port: {port.PortName}; 7E=57600, 5A A5=115200, 8N1, no flow control");
Console.WriteLine("Waiting for the six-probe handshake. Press Ctrl+C to stop.");
Console.WriteLine();

port.DiscardInBuffer();
port.DiscardOutBuffer();
ControllerSimulator simulator = new(port);

try
{
    simulator.Run(stopping.Token);
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

Console.WriteLine("Simulator stopped.");
return 0;
