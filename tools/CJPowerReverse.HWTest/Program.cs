using System.Globalization;
using System.IO.Ports;
using CJPowerReverse.HWTest;
using CJPowerReverse.Protocol;

const int BaudRate = 115200;
const int AttemptsPerParameter = 3;
TimeSpan responseTimeout = TimeSpan.FromMilliseconds(750);

if (args.Length != 1)
{
    Console.Error.WriteLine("Usage: CJPowerReverse.HWTest <COM-port>");
    Console.Error.WriteLine("Example: CJPowerReverse.HWTest COM5");
    return 2;
}

string portName = args[0];
string dtsPath = FindDtsFile();
IReadOnlyList<DtsParameter> parameters;
try
{
    parameters = DtsCatalog.Load(dtsPath);
}
catch (Exception exception)
{
    Console.Error.WriteLine($"Cannot load DTS file '{dtsPath}': {exception.Message}");
    return 3;
}

using SerialPort port = new(portName, BaudRate, Parity.None, 8, StopBits.One)
{
    Handshake = Handshake.None,
    DtrEnable = true,
    RtsEnable = false,
    ReadTimeout = 100,
    WriteTimeout = 1000
};

try
{
    port.Open();
}
catch (Exception exception)
{
    Console.Error.WriteLine($"Cannot open {portName}: {exception.Message}");
    return 4;
}

Console.WriteLine($"Port: {port.PortName} at {BaudRate} 8N1, flow control none, DTR on, RTS off");
Console.WriteLine($"DTS:  {Path.GetFullPath(dtsPath)} ({parameters.Count} entries)");
Console.WriteLine("Mode: read-only; no write or commit commands will be sent");
Console.WriteLine();

Thread.Sleep(150);
port.DiscardInBuffer();
port.DiscardOutBuffer();
SerialFrameReader reader = new(port);
Dictionary<ushort, ParameterResult> results = [];

foreach (IGrouping<ushort, DtsParameter> addressGroup in parameters.GroupBy(parameter => parameter.Address))
{
    ushort address = addressGroup.Key;
    DtsParameter representative = addressGroup.First();
    int size = ParameterValueCodec.GetSize(representative.DataType);

    if (addressGroup.Any(parameter => ParameterValueCodec.GetSize(parameter.DataType) != size))
    {
        results[address] = ParameterResult.Failure("DTS entries for this address disagree on size");
        continue;
    }

    results[address] = ReadParameter(port, reader, address, size);
}

int failures = 0;
int currentGroup = -1;
foreach (DtsParameter parameter in parameters)
{
    if (parameter.GroupIndex != currentGroup)
    {
        currentGroup = parameter.GroupIndex;
        Console.WriteLine();
        Console.WriteLine($"Group {currentGroup + 1}");
        Console.WriteLine(new string('-', 100));
    }

    ParameterResult result = results[parameter.Address];
    if (!result.Success)
    {
        failures++;
        Console.WriteLine($"{parameter.Address,5}  {parameter.Title,-32} ERROR: {result.Error}");
        continue;
    }

    long raw = ParameterValueCodec.Decode(result.Data, parameter.DataType);
    double displayed = DtsValueConverter.ToDisplayed(raw, parameter.RatioK, parameter.RatioB);
    string option = OptionText(parameter, displayed);
    string disabled = parameter.Disabled ? " [disabled]" : string.Empty;
    Console.WriteLine(
        $"{parameter.Address,5}  {parameter.Title,-32} {parameter.DataType,-3} " +
        $"raw={raw,12}  value={displayed.ToString("0.########", CultureInfo.InvariantCulture),12}" +
        $"{option}{disabled}");
}

Console.WriteLine();
Console.WriteLine($"Read {results.Count - results.Values.Count(result => !result.Success)}/{results.Count} unique addresses successfully.");
return failures == 0 ? 0 : 5;

ParameterResult ReadParameter(SerialPort serialPort, SerialFrameReader frameReader, ushort address, int size)
{
    Exception? lastError = null;
    for (int attempt = 1; attempt <= AttemptsPerParameter; attempt++)
    {
        try
        {
            byte[] request = ProtocolMessages.CreateReadRequest(address, checked((byte)size)).Encode();
            serialPort.Write(request, 0, request.Length);

            DateTime deadline = DateTime.UtcNow + responseTimeout;
            while (DateTime.UtcNow < deadline)
            {
                ProtocolFrame response = frameReader.Read(deadline - DateTime.UtcNow);
                if (response.Operation != ProtocolOperation.Read || response.Address != address)
                {
                    continue;
                }

                byte[] data = ProtocolMessages.GetReadResponseData(response, address, size);
                return ParameterResult.FromData(data);
            }
        }
        catch (Exception exception) when (exception is TimeoutException or InvalidOperationException or IOException)
        {
            lastError = exception;
        }
    }

    return ParameterResult.Failure(lastError?.Message ?? "No matching response");
}

string FindDtsFile()
{
    string currentDirectoryPath = Path.Combine(Environment.CurrentDirectory, "conReadWrite.dts");
    if (File.Exists(currentDirectoryPath))
    {
        return currentDirectoryPath;
    }

    string executablePath = Path.Combine(AppContext.BaseDirectory, "conReadWrite.dts");
    if (File.Exists(executablePath))
    {
        return executablePath;
    }

    return currentDirectoryPath;
}

static string OptionText(DtsParameter parameter, double displayed)
{
    if (parameter.Options.Count == 0 || displayed < 0 || displayed > int.MaxValue)
    {
        return string.Empty;
    }

    int index = (int)displayed;
    return index < parameter.Options.Count ? $"  ({parameter.Options[index]})" : string.Empty;
}

internal sealed record ParameterResult(bool Success, byte[] Data, string? Error)
{
    public static ParameterResult FromData(byte[] data) => new(true, data, null);
    public static ParameterResult Failure(string error) => new(false, [], error);
}
