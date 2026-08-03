using CJPowerReverse.Protocol;

var tests = new (string Name, Action Run)[]
{
    ("Captured read request", CapturedReadRequest),
    ("Captured read response", CapturedReadResponse),
    ("Captured write request", CapturedWriteRequest),
    ("Captured write acknowledgement", CapturedWriteAcknowledgement),
    ("Checksum rejection", ChecksumRejection),
    ("Signed DTS value", SignedDtsValue),
    ("Scaled DTS values", ScaledDtsValues)
};

int failures = 0;
foreach ((string name, Action run) in tests)
{
    try
    {
        run();
        Console.WriteLine($"PASS {name}");
    }
    catch (Exception exception)
    {
        failures++;
        Console.Error.WriteLine($"FAIL {name}: {exception.Message}");
    }
}

return failures == 0 ? 0 : 1;

static void CapturedReadRequest()
{
    ProtocolFrame request = ProtocolMessages.CreateReadRequest(1132, 2);
    Equal("5A A5 03 6C 84 02 88 D5 0D 0A", Hex(request.Encode()));
}

static void CapturedReadResponse()
{
    byte[] bytes = Convert.FromHexString("5AA5046C840000BA750D0A");
    ProtocolFrame response = ProtocolFrameCodec.Decode(bytes);
    Equal(ProtocolOperation.Read, response.Operation);
    Equal((ushort)1132, response.Address);
    SequenceEqual([0x00, 0x00], ProtocolMessages.GetReadResponseData(response, 1132, 2));
}

static void CapturedWriteRequest()
{
    byte[] rawValue = ParameterValueCodec.Encode(3300, ParameterDataType.U16);
    ProtocolFrame request = ProtocolMessages.CreateWriteRequest(534, rawValue);
    Equal("5A A5 04 16 22 E4 0C A2 4D 0D 0A", Hex(request.Encode()));
}

static void CapturedWriteAcknowledgement()
{
    ProtocolFrame acknowledgement = new(
        (ushort)(ProtocolConstants.WriteFlag | 534),
        [0x02, 0x00]);
    Equal((ushort)2, ProtocolMessages.GetWriteAcknowledgedByteCount(acknowledgement, 534));
}

static void ChecksumRejection()
{
    byte[] bytes = Convert.FromHexString("5AA5036C840288D50D0A");
    bytes[6] ^= 0x01;
    Equal(false, ProtocolFrameCodec.TryDecode(bytes, out _, out ProtocolDecodeError error));
    Equal(ProtocolDecodeError.ChecksumMismatch, error);
}

static void SignedDtsValue()
{
    long raw = DtsValueConverter.ToRaw(100, -10, 0);
    Equal(-1000L, raw);
    byte[] encoded = ParameterValueCodec.Encode(raw, ParameterDataType.I16);
    Equal("18 FC", Hex(encoded));
    Equal(-1000L, ParameterValueCodec.Decode(encoded, ParameterDataType.I16));
}

static void ScaledDtsValues()
{
    Equal(150L, DtsValueConverter.ToRaw(110, 1, 40));
    Equal(5000L, DtsValueConverter.ToRaw(5, 1000, 0));
    Equal(3649L, DtsValueConverter.ToRaw(4.9, 744.73, 0));
}

static void Equal<T>(T expected, T actual) where T : notnull
{
    if (!EqualityComparer<T>.Default.Equals(expected, actual))
    {
        throw new InvalidOperationException($"Expected '{expected}', received '{actual}'.");
    }
}

static void SequenceEqual(ReadOnlySpan<byte> expected, ReadOnlySpan<byte> actual)
{
    if (!expected.SequenceEqual(actual))
    {
        throw new InvalidOperationException(
            $"Expected '{Hex(expected)}', received '{Hex(actual)}'.");
    }
}

static string Hex(ReadOnlySpan<byte> data)
{
    string compact = Convert.ToHexString(data);
    return string.Join(' ', Enumerable.Range(0, compact.Length / 2)
        .Select(index => compact.Substring(index * 2, 2)));
}
