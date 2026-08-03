using System.Buffers.Binary;

namespace CJPowerReverse.Protocol;

/// <summary>Encodes and decodes the little-endian integer types used by DTS parameters.</summary>
public static class ParameterValueCodec
{
    public static int GetSize(ParameterDataType type) => type switch
    {
        ParameterDataType.U8 or ParameterDataType.I8 => 1,
        ParameterDataType.U16 or ParameterDataType.I16 => 2,
        ParameterDataType.U32 or ParameterDataType.I32 => 4,
        _ => throw new ArgumentOutOfRangeException(nameof(type))
    };

    public static ParameterDataType Parse(string value) => value.Trim().ToLowerInvariant() switch
    {
        "u8" => ParameterDataType.U8,
        "i8" => ParameterDataType.I8,
        "u16" => ParameterDataType.U16,
        "i16" => ParameterDataType.I16,
        "u32" => ParameterDataType.U32,
        "i32" => ParameterDataType.I32,
        _ => throw new FormatException($"Unsupported DTS parameter type '{value}'.")
    };

    public static long Decode(ReadOnlySpan<byte> data, ParameterDataType type)
    {
        int size = GetSize(type);
        if (data.Length != size)
        {
            throw new ArgumentException($"{type} requires exactly {size} bytes.", nameof(data));
        }

        return type switch
        {
            ParameterDataType.U8 => data[0],
            ParameterDataType.I8 => unchecked((sbyte)data[0]),
            ParameterDataType.U16 => BinaryPrimitives.ReadUInt16LittleEndian(data),
            ParameterDataType.I16 => BinaryPrimitives.ReadInt16LittleEndian(data),
            ParameterDataType.U32 => BinaryPrimitives.ReadUInt32LittleEndian(data),
            ParameterDataType.I32 => BinaryPrimitives.ReadInt32LittleEndian(data),
            _ => throw new ArgumentOutOfRangeException(nameof(type))
        };
    }

    public static byte[] Encode(long value, ParameterDataType type)
    {
        byte[] data = new byte[GetSize(type)];
        switch (type)
        {
            case ParameterDataType.U8:
                data[0] = checked((byte)value);
                break;
            case ParameterDataType.I8:
                data[0] = unchecked((byte)checked((sbyte)value));
                break;
            case ParameterDataType.U16:
                BinaryPrimitives.WriteUInt16LittleEndian(data, checked((ushort)value));
                break;
            case ParameterDataType.I16:
                BinaryPrimitives.WriteInt16LittleEndian(data, checked((short)value));
                break;
            case ParameterDataType.U32:
                BinaryPrimitives.WriteUInt32LittleEndian(data, checked((uint)value));
                break;
            case ParameterDataType.I32:
                BinaryPrimitives.WriteInt32LittleEndian(data, checked((int)value));
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(type));
        }

        return data;
    }
}
