namespace CJPowerReverse.Protocol;

/// <summary>CRC-16/CCITT-FALSE: polynomial 0x1021 and initial value 0xFFFF.</summary>
public static class Crc16CcittFalse
{
    public const ushort Polynomial = 0x1021;
    public const ushort InitialValue = 0xFFFF;

    public static ushort Compute(ReadOnlySpan<byte> data)
    {
        ushort crc = InitialValue;

        foreach (byte value in data)
        {
            crc ^= (ushort)(value << 8);
            for (int bit = 0; bit < 8; bit++)
            {
                crc = (crc & 0x8000) != 0
                    ? (ushort)((crc << 1) ^ Polynomial)
                    : (ushort)(crc << 1);
            }
        }

        return crc;
    }
}
