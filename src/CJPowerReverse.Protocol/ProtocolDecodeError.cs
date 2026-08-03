namespace CJPowerReverse.Protocol;

public enum ProtocolDecodeError
{
    None,
    TooShort,
    InvalidHeader,
    InvalidLength,
    InvalidTerminator,
    ChecksumMismatch
}
