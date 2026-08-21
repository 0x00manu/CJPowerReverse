using System.Buffers.Binary;
using System.IO.Ports;
using System.Text;

internal sealed class ControllerSimulator(SerialPort port)
{
    private static readonly byte[][] VerificationProbes =
    [
        Hex("7E00020804C09000200082FE0D"),
        Hex("7E00030102E0C0C09AFD0D"),
        Hex("7E00030102E0C0C09AFD0D"),
        Hex("7E00030102E0C0C09AFD0D"),
        Hex("7E00030102E0C0C09AFD0D"),
        Hex("7E00030204C030002000E7FE0D")
    ];

    private static readonly byte[] IdentificationRequest = Hex("5AA503058010C52E0D0A");
    private static readonly byte[] BatchRequest = Hex(
        "7E0002024B10" +
        "390001F50001C00001CB0001690002EF0004770002710002E00002730002" +
        "DC0002DE0001DF00015A00016A04026C0402E80001E90001EA0001EB0001" +
        "EC0001ED0001EE0001F30001F40001" +
        "0CED0D");

    private readonly List<byte> input = [];
    private readonly DateTime started = DateTime.UtcNow;
    private int handshakeProbeIndex;
    private bool handshakeComplete;
    private long requestCount;

    public void Run(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                int value = port.ReadByte();
                if (value >= 0)
                {
                    Push((byte)value);
                }
            }
            catch (TimeoutException)
            {
                // Short reads keep Ctrl+C responsive.
            }
        }
    }

    private void Push(byte value)
    {
        if (input.Count == 0 && value is not 0x7E and not 0x5A)
        {
            return;
        }

        input.Add(value);
        if (input[0] == 0x5A)
        {
            if (input.Count == 2 && input[1] != 0xA5)
            {
                input.Clear();
                return;
            }

            if (input.Count >= 3)
            {
                int expectedLength = input[2] + 7;
                if (input.Count == expectedLength)
                {
                    Process5AA5(input.ToArray());
                    input.Clear();
                }
                else if (input.Count > expectedLength)
                {
                    input.Clear();
                }
            }
            return;
        }

        if (value == 0x0D)
        {
            Process7E(input.ToArray());
            input.Clear();
        }
        else if (input.Count > 96)
        {
            input.Clear();
        }
    }

    private void Process7E(byte[] frame)
    {
        if (handshakeProbeIndex < VerificationProbes.Length &&
            frame.AsSpan().SequenceEqual(VerificationProbes[handshakeProbeIndex]))
        {
            handshakeProbeIndex++;
            Console.WriteLine($"Handshake probe {handshakeProbeIndex}/6 received");
            if (handshakeProbeIndex == VerificationProbes.Length)
            {
                // The client changes from the 7E baud to the 5A A5 baud after
                // the final probe and before sending the identity request.
                port.BaudRate = 115200;
                Console.WriteLine("Serial baud changed to 115200 for identification");
            }
            return;
        }

        if (frame.AsSpan().SequenceEqual(VerificationProbes[0]))
        {
            handshakeComplete = false;
            handshakeProbeIndex = 1;
            Console.WriteLine("Handshake restarted; probe 1/6 received");
            return;
        }

        if (handshakeComplete && frame.AsSpan().SequenceEqual(BatchRequest))
        {
            Send(BuildBatchResponse());
            Console.WriteLine($"7E batch response #{++requestCount}");
            return;
        }

        Console.WriteLine($"Ignored 7E frame ({frame.Length} bytes); handshake complete={handshakeComplete}");
    }

    private void Process5AA5(byte[] frame)
    {
        if (!Validate5AA5(frame))
        {
            Console.WriteLine("Ignored invalid 5A A5 frame");
            return;
        }

        if (handshakeProbeIndex == VerificationProbes.Length &&
            frame.AsSpan().SequenceEqual(IdentificationRequest))
        {
            byte[] identity = Encoding.ASCII.GetBytes("B1.T2.009-031A");
            Send(Build5AA5Response(0x8005, identity));
            // Allow the response to leave the UART before following the ESP8266
            // back to the 57600-baud live batch protocol.
            Thread.Sleep(20);
            port.BaudRate = 57600;
            handshakeComplete = true;
            Console.WriteLine("Handshake successful; identity B1.T2.009-031A returned");
            Console.WriteLine("Serial baud changed to 57600 for dashboard batches");
            return;
        }

        if (!handshakeComplete)
        {
            Console.WriteLine("Ignored register request before successful handshake");
            return;
        }

        ushort encodedAddress = BinaryPrimitives.ReadUInt16LittleEndian(frame.AsSpan(3));
        if ((encodedAddress & 0xC000) != 0x8000 || frame.Length != 10)
        {
            Console.WriteLine($"Ignored non-read request 0x{encodedAddress:X4}");
            return;
        }

        ushort address = (ushort)(encodedAddress & 0x3FFF);
        byte requestedSize = frame[5];
        byte[]? value = GetRegister(address, requestedSize);
        if (value is null)
        {
            Console.WriteLine($"No simulated value for address {address}, size {requestedSize}");
            return;
        }

        Send(Build5AA5Response(encodedAddress, value));
        requestCount++;
    }

    private byte[] BuildBatchResponse()
    {
        byte[] data = BuildDashboardData();
        byte[] frame = new byte[45];
        byte[] prefix = [0x7E, 0x00, 0x02, 0x82, 0x24, 0xA0];
        prefix.CopyTo(frame, 0);
        data.CopyTo(frame, 6);
        ushort checksum = Compute7EChecksum(frame.AsSpan(0, 42));
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(42), checksum);
        frame[44] = 0x0D;
        return frame;
    }

    private byte[]? GetRegister(ushort address, byte size)
    {
        byte[] data = BuildDashboardData();
        (int Offset, int Size)? location = address switch
        {
            57 => (0, 1), 245 => (1, 1), 192 => (2, 1), 203 => (3, 1),
            105 => (4, 2), 239 => (6, 4), 119 => (10, 2), 113 => (12, 2),
            224 => (14, 2), 115 => (16, 2), 220 => (18, 2), 222 => (20, 1),
            223 => (21, 1), 90 => (22, 1), 1130 => (23, 2), 1132 => (25, 2),
            232 => (27, 1), 233 => (28, 1), 234 => (29, 1), 235 => (30, 1),
            236 => (31, 1), 237 => (32, 1), 238 => (33, 1), 243 => (34, 1),
            244 => (35, 1), _ => null
        };
        return location is { } item && item.Size == size
            ? data.AsSpan(item.Offset, item.Size).ToArray()
            : null;
    }

    private byte[] BuildDashboardData()
    {
        double seconds = (DateTime.UtcNow - started).TotalSeconds;
        short speed = (short)Math.Max(0, Math.Round(430 + 400 * Math.Sin(seconds / 3)));
        short dcCurrent = (short)Math.Round(speed / 20.0);
        ushort dcVoltage = (ushort)Math.Round(510 + 4 * Math.Sin(seconds / 8));
        ushort throttle = (ushort)Math.Round(560 + speed * 0.45);
        byte[] data = new byte[36];
        data[0] = speed > 20 ? (byte)2 : (byte)0;
        data[1] = speed > 20 ? (byte)2 : (byte)0;
        data[2] = speed > 600 ? (byte)2 : (byte)1;
        data[3] = 0;
        BinaryPrimitives.WriteInt16LittleEndian(data.AsSpan(4), speed);
        BinaryPrimitives.WriteUInt32LittleEndian(data.AsSpan(6), 0);
        BinaryPrimitives.WriteInt16LittleEndian(data.AsSpan(10), dcCurrent);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(12), dcVoltage);
        BinaryPrimitives.WriteInt16LittleEndian(data.AsSpan(14), (short)(speed / 5));
        BinaryPrimitives.WriteInt16LittleEndian(data.AsSpan(16), 0);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(18), throttle);
        data[20] = 75;
        data[21] = 78;
        data[22] = (byte)(((int)seconds % 6) + 1);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(23), 1282);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(25), 0);
        data[30] = speed > 700 ? (byte)1 : (byte)0;
        data[33] = speed > 800 ? (byte)1 : (byte)0;
        return data;
    }

    private void Send(byte[] frame)
    {
        port.Write(frame, 0, frame.Length);
        port.BaseStream.Flush();
    }

    private static byte[] Build5AA5Response(ushort encodedAddress, ReadOnlySpan<byte> body)
    {
        byte[] frame = new byte[body.Length + 9];
        frame[0] = 0x5A;
        frame[1] = 0xA5;
        frame[2] = checked((byte)(body.Length + 2));
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(3), encodedAddress);
        body.CopyTo(frame.AsSpan(5));
        ushort crc = ComputeCrc(frame.AsSpan(3, body.Length + 2));
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(5 + body.Length), crc);
        frame[^2] = 0x0D;
        frame[^1] = 0x0A;
        return frame;
    }

    private static bool Validate5AA5(ReadOnlySpan<byte> frame)
    {
        if (frame.Length < 9 || frame[0] != 0x5A || frame[1] != 0xA5 ||
            frame.Length != frame[2] + 7 || frame[^2] != 0x0D || frame[^1] != 0x0A)
        {
            return false;
        }
        ushort actual = BinaryPrimitives.ReadUInt16LittleEndian(frame[^4..^2]);
        return actual == ComputeCrc(frame.Slice(3, frame[2]));
    }

    private static ushort Compute7EChecksum(ReadOnlySpan<byte> frameBeforeChecksum)
    {
        ushort sum = 0;
        for (int index = 1; index < frameBeforeChecksum.Length; index++)
        {
            sum = unchecked((ushort)(sum + frameBeforeChecksum[index]));
        }
        return unchecked((ushort)(0 - sum));
    }

    private static ushort ComputeCrc(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;
        foreach (byte value in data)
        {
            crc ^= (ushort)(value << 8);
            for (int bit = 0; bit < 8; bit++)
            {
                crc = (crc & 0x8000) != 0
                    ? (ushort)((crc << 1) ^ 0x1021)
                    : (ushort)(crc << 1);
            }
        }
        return crc;
    }

    private static byte[] Hex(string value) => Convert.FromHexString(value);
}
