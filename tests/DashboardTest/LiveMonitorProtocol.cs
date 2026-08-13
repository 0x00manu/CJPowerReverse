using System.Buffers.Binary;

internal static class LiveMonitorProtocol
{
    public const byte Header = 0x7E;
    public const int ResponseSize = 45;

    public static readonly byte[] BatchReadRequest = Convert.FromHexString(
        "7E0002024B10" +
        "390001F50001C00001CB0001690002EF0004770002710002E00002730002" +
        "DC0002DE0001DF00015A00016A04026C0402E80001E90001EA0001EB0001" +
        "EC0001ED0001EE0001F30001F40001" +
        "0CED0D");

    // Exact protocol-verification probes observed in doc/HandShake.pcapng.
    public static readonly byte[][] VerificationProbes =
    [
        Convert.FromHexString("7E00020804C09000200082FE0D"),
        Convert.FromHexString("7E00030102E0C0C09AFD0D"),
        Convert.FromHexString("7E00030102E0C0C09AFD0D"),
        Convert.FromHexString("7E00030102E0C0C09AFD0D"),
        Convert.FromHexString("7E00030102E0C0C09AFD0D"),
        Convert.FromHexString("7E00030204C030002000E7FE0D")
    ];

    public static readonly byte[] IdentificationRequest =
        Convert.FromHexString("5AA503058010C52E0D0A");

    public static string DecodeIdentification(ReadOnlySpan<byte> frame)
    {
        // The request asks for up to 16 bytes; the captured response LEN=0x10
        // contains a two-byte address followed by a 14-byte identity string.
        ReadOnlySpan<byte> body = DecodeRegisterResponse(frame, 5, 14);
        return System.Text.Encoding.ASCII.GetString(body).TrimEnd('\0');
    }

    public static byte[] BuildRegisterRead(ushort address, byte size)
    {
        ushort encodedAddress = checked((ushort)(0x8000 | address));
        byte[] frame = new byte[10];
        frame[0] = 0x5A;
        frame[1] = 0xA5;
        frame[2] = 3;
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(3), encodedAddress);
        frame[5] = size;
        ushort crc = ComputeCrc16CcittFalse(frame.AsSpan(3, 3));
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(6), crc);
        frame[8] = 0x0D;
        frame[9] = 0x0A;
        return frame;
    }

    public static byte[] DecodeRegisterResponse(ReadOnlySpan<byte> frame, ushort address, int size)
    {
        int expectedLength = size + 9;
        ushort encodedAddress = checked((ushort)(0x8000 | address));
        if (frame.Length != expectedLength || frame[0] != 0x5A || frame[1] != 0xA5 ||
            frame[2] != size + 2 || BinaryPrimitives.ReadUInt16LittleEndian(frame[3..]) != encodedAddress ||
            frame[^2] != 0x0D || frame[^1] != 0x0A)
        {
            throw new FormatException($"unexpected 5A A5 response for address {address}");
        }

        ushort expectedCrc = ComputeCrc16CcittFalse(frame.Slice(3, size + 2));
        ushort receivedCrc = BinaryPrimitives.ReadUInt16LittleEndian(frame.Slice(5 + size, 2));
        if (expectedCrc != receivedCrc)
        {
            throw new FormatException($"5A A5 CRC mismatch for address {address}");
        }

        return frame.Slice(5, size).ToArray();
    }

    public static DashboardSnapshot DecodeRegisterSnapshot(IReadOnlyDictionary<ushort, byte[]> values)
    {
        byte U8(ushort address) => values[address][0];
        short I16(ushort address) => BinaryPrimitives.ReadInt16LittleEndian(values[address]);
        ushort U16(ushort address) => BinaryPrimitives.ReadUInt16LittleEndian(values[address]);
        uint U32(ushort address) => BinaryPrimitives.ReadUInt32LittleEndian(values[address]);

        return new DashboardSnapshot(
            U8(57), U8(245), U8(192), U8(203), I16(105), U32(239),
            I16(119) / 10.0, U16(113) / 10.0, I16(224), I16(115),
            U16(220) / 744.73, U8(222) - 40, U8(223) - 40, U8(90),
            U16(1130), U16(1132), U8(232) != 0, U8(233) != 0,
            U8(234) != 0, U8(235) != 0, U8(236) != 0, U8(237) != 0,
            U8(238) != 0, U8(243) != 0, U8(244) != 0);
    }

    public static readonly (ushort Address, byte Size)[] DashboardRegisters =
    [
        (57, 1), (245, 1), (192, 1), (203, 1), (105, 2), (239, 4),
        (119, 2), (113, 2), (224, 2), (115, 2), (220, 2), (222, 1),
        (223, 1), (90, 1), (1130, 2), (1132, 2), (232, 1), (233, 1),
        (234, 1), (235, 1), (236, 1), (237, 1), (238, 1), (243, 1), (244, 1)
    ];

    public static DashboardSnapshot DecodeResponse(ReadOnlySpan<byte> frame)
    {
        if (frame.Length != ResponseSize ||
            !frame[..6].SequenceEqual(new byte[] { 0x7E, 0x00, 0x02, 0x82, 0x24, 0xA0 }) ||
            frame[^1] != 0x0D)
        {
            throw new FormatException("unexpected 7E response header, length, or terminator");
        }

        ushort expectedChecksum = ComputeChecksum(frame[..^3]);
        ushort receivedChecksum = BinaryPrimitives.ReadUInt16LittleEndian(frame[^3..^1]);
        if (expectedChecksum != receivedChecksum)
        {
            throw new FormatException(
                $"checksum mismatch: expected 0x{expectedChecksum:X4}, received 0x{receivedChecksum:X4}");
        }

        ReadOnlySpan<byte> data = frame.Slice(6, 36);
        return new DashboardSnapshot(
            DriveStateRaw: data[0],
            RunningStateRaw: data[1],
            VehicleGearRaw: data[2],
            VehicleModeRaw: data[3],
            MotorSpeedRpm: BinaryPrimitives.ReadInt16LittleEndian(data[4..]),
            FaultCode: BinaryPrimitives.ReadUInt32LittleEndian(data[6..]),
            DcCurrentA: BinaryPrimitives.ReadInt16LittleEndian(data[10..]) / 10.0,
            ControllerVoltageV: BinaryPrimitives.ReadUInt16LittleEndian(data[12..]) / 10.0,
            PhaseCurrentRaw: BinaryPrimitives.ReadInt16LittleEndian(data[14..]),
            MotorPowerRaw: BinaryPrimitives.ReadInt16LittleEndian(data[16..]),
            ThrottleVoltageV: BinaryPrimitives.ReadUInt16LittleEndian(data[18..]) / 744.73,
            McuTemperatureC: data[20] - 40,
            MotorTemperatureC: data[21] - 40,
            HallValue: data[22],
            P1: BinaryPrimitives.ReadUInt16LittleEndian(data[23..]),
            P2: BinaryPrimitives.ReadUInt16LittleEndian(data[25..]),
            Brake: data[27] != 0,
            AntiTheft: data[28] != 0,
            Park: data[29] != 0,
            Cruise: data[30] != 0,
            Reverse: data[31] != 0,
            SideStand: data[32] != 0,
            Boost: data[33] != 0,
            AccGear: data[34] != 0,
            DecGear: data[35] != 0);
    }

    private static ushort ComputeChecksum(ReadOnlySpan<byte> frameBeforeChecksum)
    {
        ushort sum = 0;
        for (int index = 1; index < frameBeforeChecksum.Length; index++)
        {
            sum = unchecked((ushort)(sum + frameBeforeChecksum[index]));
        }

        return unchecked((ushort)(0 - sum));
    }

    private static ushort ComputeCrc16CcittFalse(ReadOnlySpan<byte> data)
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
}

internal enum ControllerProtocol
{
    Batch7E,
    Register5AA5
}

internal sealed record DashboardSnapshot(
    byte DriveStateRaw,
    byte RunningStateRaw,
    byte VehicleGearRaw,
    byte VehicleModeRaw,
    short MotorSpeedRpm,
    uint FaultCode,
    double DcCurrentA,
    double ControllerVoltageV,
    short PhaseCurrentRaw,
    short MotorPowerRaw,
    double ThrottleVoltageV,
    int McuTemperatureC,
    int MotorTemperatureC,
    byte HallValue,
    ushort P1,
    ushort P2,
    bool Brake,
    bool AntiTheft,
    bool Park,
    bool Cruise,
    bool Reverse,
    bool SideStand,
    bool Boost,
    bool AccGear,
    bool DecGear);
