using System.Diagnostics;
using System.IO.Ports;
using System.Runtime.InteropServices;
using CJPowerReverse.Protocol;

namespace CJPowerReverse.HWTest;

internal sealed class SerialFrameReader(SerialPort port)
{
    public ProtocolFrame Read(TimeSpan timeout)
    {
        Stopwatch timer = Stopwatch.StartNew();
        List<byte> candidate = [];

        while (timer.Elapsed < timeout)
        {
            int value;
            try
            {
                value = port.ReadByte();
            }
            catch (TimeoutException)
            {
                continue;
            }

            byte current = checked((byte)value);
            if (candidate.Count == 0)
            {
                if (current == ProtocolConstants.HeaderFirst)
                {
                    candidate.Add(current);
                }

                continue;
            }

            if (candidate.Count == 1)
            {
                if (current == ProtocolConstants.HeaderSecond)
                {
                    candidate.Add(current);
                }
                else if (current != ProtocolConstants.HeaderFirst)
                {
                    candidate.Clear();
                }

                continue;
            }

            candidate.Add(current);
            if (candidate.Count < 3)
            {
                continue;
            }

            int expectedLength = candidate[2] + ProtocolConstants.FixedOverhead;
            if (candidate.Count < expectedLength)
            {
                continue;
            }

            if (ProtocolFrameCodec.TryDecode(CollectionsMarshal.AsSpan(candidate),
                    out ProtocolFrame? frame, out _))
            {
                return frame!;
            }

            candidate.Clear();
            if (current == ProtocolConstants.HeaderFirst)
            {
                candidate.Add(current);
            }
        }

        throw new TimeoutException($"No valid protocol frame was received within {timeout.TotalMilliseconds:0} ms.");
    }
}
