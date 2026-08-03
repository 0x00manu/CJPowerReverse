namespace CJPowerReverse.Protocol;

/// <summary>Constants confirmed from the captured application protocol.</summary>
public static class ProtocolConstants
{
    public const byte HeaderFirst = 0x5A;
    public const byte HeaderSecond = 0xA5;
    public const byte TerminatorFirst = 0x0D;
    public const byte TerminatorSecond = 0x0A;

    public const ushort ReadFlag = 0x8000;
    public const ushort WriteFlag = 0x2000;
    public const ushort OperationMask = 0xE000;
    public const ushort AddressMask = 0x1FFF;

    public const int FixedOverhead = 7;
    public const int AddressSize = 2;
    public const int MaximumBodySize = byte.MaxValue - AddressSize;
}
