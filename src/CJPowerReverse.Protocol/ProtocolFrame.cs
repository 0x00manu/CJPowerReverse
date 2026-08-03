namespace CJPowerReverse.Protocol;

/// <summary>A validated application frame with its encoded address and body.</summary>
public sealed class ProtocolFrame
{
    public ProtocolFrame(ushort rawAddress, ReadOnlySpan<byte> body)
    {
        RawAddress = rawAddress;
        Body = body.ToArray();
    }

    public ushort RawAddress { get; }

    public ushort Address => (ushort)(RawAddress & ProtocolConstants.AddressMask);

    public ushort OperationBits => (ushort)(RawAddress & ProtocolConstants.OperationMask);

    public ProtocolOperation Operation => OperationBits switch
    {
        0 => ProtocolOperation.Unflagged,
        ProtocolConstants.ReadFlag => ProtocolOperation.Read,
        ProtocolConstants.WriteFlag => ProtocolOperation.Write,
        _ => ProtocolOperation.Unknown
    };

    public byte[] Body { get; }

    public byte[] Encode() => ProtocolFrameCodec.Encode(RawAddress, Body);
}
