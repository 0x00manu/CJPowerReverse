using System.Buffers.Binary;

namespace CJPowerReverse.Protocol;

/// <summary>Encodes and validates complete 5A-A5 protocol frames.</summary>
public static class ProtocolFrameCodec
{
    public static byte[] Encode(ushort rawAddress, ReadOnlySpan<byte> body)
    {
        if (body.Length > ProtocolConstants.MaximumBodySize)
        {
            throw new ArgumentOutOfRangeException(nameof(body), body.Length,
                $"A frame body cannot exceed {ProtocolConstants.MaximumBodySize} bytes.");
        }

        int payloadLength = ProtocolConstants.AddressSize + body.Length;
        byte[] frame = new byte[payloadLength + ProtocolConstants.FixedOverhead];
        frame[0] = ProtocolConstants.HeaderFirst;
        frame[1] = ProtocolConstants.HeaderSecond;
        frame[2] = checked((byte)payloadLength);
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(3, 2), rawAddress);
        body.CopyTo(frame.AsSpan(5));

        ushort crc = Crc16CcittFalse.Compute(frame.AsSpan(3, payloadLength));
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(3 + payloadLength, 2), crc);
        frame[^2] = ProtocolConstants.TerminatorFirst;
        frame[^1] = ProtocolConstants.TerminatorSecond;
        return frame;
    }

    public static ProtocolFrame Decode(ReadOnlySpan<byte> data)
    {
        if (!TryDecode(data, out ProtocolFrame? frame, out ProtocolDecodeError error))
        {
            throw new FormatException($"Invalid protocol frame: {error}.");
        }

        return frame!;
    }

    public static bool TryDecode(
        ReadOnlySpan<byte> data,
        out ProtocolFrame? frame,
        out ProtocolDecodeError error)
    {
        frame = null;

        if (data.Length < ProtocolConstants.AddressSize + ProtocolConstants.FixedOverhead)
        {
            error = ProtocolDecodeError.TooShort;
            return false;
        }

        if (data[0] != ProtocolConstants.HeaderFirst || data[1] != ProtocolConstants.HeaderSecond)
        {
            error = ProtocolDecodeError.InvalidHeader;
            return false;
        }

        int payloadLength = data[2];
        if (payloadLength < ProtocolConstants.AddressSize ||
            data.Length != payloadLength + ProtocolConstants.FixedOverhead)
        {
            error = ProtocolDecodeError.InvalidLength;
            return false;
        }

        if (data[^2] != ProtocolConstants.TerminatorFirst ||
            data[^1] != ProtocolConstants.TerminatorSecond)
        {
            error = ProtocolDecodeError.InvalidTerminator;
            return false;
        }

        ushort expected = Crc16CcittFalse.Compute(data.Slice(3, payloadLength));
        ushort actual = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(3 + payloadLength, 2));
        if (expected != actual)
        {
            error = ProtocolDecodeError.ChecksumMismatch;
            return false;
        }

        ushort rawAddress = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(3, 2));
        frame = new ProtocolFrame(rawAddress, data.Slice(5, payloadLength - 2));
        error = ProtocolDecodeError.None;
        return true;
    }
}
