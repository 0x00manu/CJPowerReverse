namespace CJPowerReverse.Protocol;

/// <summary>Converts between DTS display values and raw integer register values.</summary>
public static class DtsValueConverter
{
    public static long ToRaw(
        double displayedValue,
        double ratioK = 1,
        double ratioB = 0,
        MidpointRounding rounding = MidpointRounding.AwayFromZero)
    {
        double raw = (displayedValue * ratioK) + ratioB;
        if (!double.IsFinite(raw) || raw < long.MinValue || raw > long.MaxValue)
        {
            throw new OverflowException("The scaled DTS value is outside the Int64 range.");
        }

        return checked((long)Math.Round(raw, 0, rounding));
    }

    public static double ToDisplayed(long rawValue, double ratioK = 1, double ratioB = 0)
    {
        if (ratioK == 0 || !double.IsFinite(ratioK) || !double.IsFinite(ratioB))
        {
            throw new ArgumentOutOfRangeException(nameof(ratioK), "DTS scaling must be finite and ratio_K cannot be zero.");
        }

        return (rawValue - ratioB) / ratioK;
    }
}
