using System;
using System.IO;
using System.IO.Ports;
using System.Threading;
using System.Threading.Tasks;

public class Program
{
    public static async Task Main()
    {
        // The external application owns COM20. Because COM20 and COM21 form a
        // com0com pair, this bridge must open COM21 and must never open COM20.
        // COM5 is the physical USB-to-serial port connected to the controller.
        using (var physical = CreatePort("COM5", true))
        using (var virtualPort = CreatePort("COM21", false))
        using (var cancellation = new CancellationTokenSource())
        {
            // Convert Ctrl+C into cooperative cancellation so both forwarding
            // loops stop and the using blocks release the serial ports.
            Console.CancelKeyPress += delegate(object sender, ConsoleCancelEventArgs eventArgs)
            {
                eventArgs.Cancel = true;
                cancellation.Cancel();
            };

            physical.Open();
            virtualPort.Open();

            Console.WriteLine("Bridging application COM20 to physical COM5 via COM21.");
            Console.WriteLine("Press Ctrl+C to stop.");

            try
            {
                // Relay controller responses from physical COM5 to the
                // application's COM20 endpoint through virtual COM21.
                // Task-1
                Task deviceToApp = ForwardAsync(
                    physical.BaseStream,
                    virtualPort.BaseStream,
                    "DEVICE -> APP",
                    cancellation.Token);

                // Relay application requests received on virtual COM21 to the
                // controller attached to physical COM5.
                // Task-2
                Task appToDevice = ForwardAsync(
                    virtualPort.BaseStream,
                    physical.BaseStream,
                    "APP -> DEVICE",
                    cancellation.Token);

                await Task.WhenAll(deviceToApp, appToDevice);
            }
            catch (OperationCanceledException)
            {
                // Expected when Ctrl+C is pressed.
            }
        }
    }

    private static SerialPort CreatePort(string portName, bool controlDeviceLines)
    {
        // Both bridge endpoints use the controller's confirmed 115200 8N1
        // configuration with no hardware or software flow control.
        return new SerialPort(portName, 115200, Parity.None, 8, StopBits.One)
        {
            Handshake = Handshake.None,
            // Assert DTR only on the physical controller connection. Modem
            // control changes made by the application are not propagated.
            DtrEnable = controlDeviceLines,
            RtsEnable = false,
            ReadTimeout = SerialPort.InfiniteTimeout,
            WriteTimeout = 2000
        };
    }
    /// <summary>
    /// The Taks
    /// </summary>
    private static async Task ForwardAsync(
        Stream input,
        Stream output,
        string direction,
        CancellationToken cancellationToken)
    {
        // A read can return any available number of bytes; preserve the exact
        // bytes and order rather than assuming protocol-frame boundaries.
        var buffer = new byte[1024];

        while (!cancellationToken.IsCancellationRequested)
        {
            int count = await input.ReadAsync(
                buffer,
                0,
                buffer.Length,
                cancellationToken);

            if (count == 0)
                return;

            // Log a timestamped hex copy without writing diagnostics to either
            // serial port. Direction is expressed from the application's view.
            Console.WriteLine(
                "{0:O} {1,-13} {2}",
                DateTimeOffset.Now,
                direction,
                BitConverter.ToString(buffer, 0, count).Replace('-', ' '));

            // Forward exactly the bytes returned by this read.
            await output.WriteAsync(buffer, 0, count, cancellationToken);
            await output.FlushAsync(cancellationToken);
        }
    }
}
