using System.Buffers.Binary;

namespace CJPowerReverse.Protocol;

/// <summary>Constructs and interprets confirmed read and write messages.</summary>
public static class ProtocolMessages
{
    public static ProtocolFrame CreateReadRequest(ushort address, byte byteCount)
    {
        ValidateAddress(address);
        if (byteCount == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(byteCount), "Read length must be nonzero.");
        }

        return new ProtocolFrame((ushort)(ProtocolConstants.ReadFlag | address), [byteCount]);
    }

    public static ProtocolFrame CreateWriteRequest(ushort address, ReadOnlySpan<byte> value)
    {
        ValidateAddress(address);
        if (value.IsEmpty)
        {
            throw new ArgumentException("A write must contain at least one byte.", nameof(value));
        }

        return new ProtocolFrame((ushort)(ProtocolConstants.WriteFlag | address), value);
    }

    public static byte[] GetReadResponseData(ProtocolFrame response, ushort expectedAddress, int expectedLength)
    {
        ArgumentNullException.ThrowIfNull(response);
        if (response.Operation != ProtocolOperation.Read || response.Address != expectedAddress)
        {
            throw new InvalidOperationException("The response does not match the expected read address.");
        }

        if (response.Body.Length != expectedLength)
        {
            throw new InvalidOperationException(
                $"Expected {expectedLength} response bytes but received {response.Body.Length}.");
        }

        return response.Body.ToArray();
    }

    public static ushort GetWriteAcknowledgedByteCount(ProtocolFrame response, ushort expectedAddress)
    {
        ArgumentNullException.ThrowIfNull(response);
        if (response.Operation != ProtocolOperation.Write || response.Address != expectedAddress)
        {
            throw new InvalidOperationException("The response does not match the expected write address.");
        }

        if (response.Body.Length != 2)
        {
            throw new InvalidOperationException("A write acknowledgement must contain a 16-bit byte count.");
        }

        return BinaryPrimitives.ReadUInt16LittleEndian(response.Body);
    }

    private static void ValidateAddress(ushort address)
    {
        if ((address & ~ProtocolConstants.AddressMask) != 0)
        {
            throw new ArgumentOutOfRangeException(nameof(address), address,
                $"Parameter addresses must fit in {ProtocolConstants.AddressMask:X4}.");
        }
    }
}
