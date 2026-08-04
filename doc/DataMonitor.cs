class DataMonitor
{
    public DataView dataView { get; set; }
    public ParameterView parameterView { get; set; }
    public FunctionKeys functionKeys { get; set; }
}
public enum DriveState
{
    Read = 0,
    Drive = 1,
    Reverse = 2
}
public enum RunningState
{
    Stop = 0,
    Run = 1,
    MTPA = 2,
}
public enum VehicleGear
{
    Gear1 = 1,
    Gear2 = 2,
    Gear3 = 3,
}
public enum VehicleMode
{
    Running = 0,
    Parked = 1,
    Halt = 2,
}
public class DataView
{
    public DriveState driveState { get; set; }
    public RunningState runningState { get; set; }
    public VehicleGear vehicleGear { get; set; }
    public VehicleMode vehicleMode { get; set; }
}

public class ParameterView
{
    public short MotorSpd { get; set; }
    public short FaultCode { get; set; }
    public short DcCurrent { get; set; }
    public short DcVoltage { get; set; }
    public short PhaseCurrent { get; set; }
    public short MotorPower { get; set; }
    public short ThrottleVoltage { get; set; }
    public short MCUTemperature { get; set; }
    public short MotorTemperature { get; set; }
    public short HallValue { get; set; }
    public short P1 { get; set; }
    public short P2 { get; set; }
}

public class FunctionKeys
{
    public bool Brake { get; set; }
    public bool AntiTheft { get; set; }
    public bool Park { get; set; }
    public bool Cruise { get; set; }
    public bool Reverse { get; set; }
    public bool SideStand { get; set; }
    public bool BOOST { get; set; }
    public bool AccGear { get; set; }
    public bool DecGear { get; set; }
}