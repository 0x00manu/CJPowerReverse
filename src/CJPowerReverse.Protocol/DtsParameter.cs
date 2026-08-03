namespace CJPowerReverse.Protocol;

/// <summary>One parameter definition loaded from a DTS file.</summary>
public sealed record DtsParameter(
    int GroupIndex,
    string Title,
    ushort Address,
    ParameterDataType DataType,
    double RatioK,
    double RatioB,
    bool Disabled,
    IReadOnlyList<string> Options);
