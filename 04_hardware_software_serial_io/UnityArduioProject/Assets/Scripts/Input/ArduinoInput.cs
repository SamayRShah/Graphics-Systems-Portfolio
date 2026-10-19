using System;
using System.IO.Ports;
using System.Threading;
using UnityEngine;

public class ArduinoInput : MonoBehaviour
{

    #region Singleton Boilerplate
    // life-cycle
    private static bool _running;
    private static ArduinoInput _instance;
    public static ArduinoInput Instance
    {
        get
        {
            if(_instance != null) return _instance;

            _instance = FindAnyObjectByType<ArduinoInput>();
            if (_instance != null) return _instance;

            GameObject singletonObject = Instantiate( new GameObject("ArduinoInput"));
            _instance = singletonObject.AddComponent<ArduinoInput>();
            DontDestroyOnLoad(singletonObject);
            return _instance;
        }
    }
    #endregion

    // port initialization data
    [SerializeField] private SerialPort _dataStream;
    [SerializeField] private string _serialPort = "COM3";
    [SerializeField] private int _baud = 9600;
    [SerializeField] private float _minStep = 0; // to remove noise
    [SerializeField] private Vector3 _ledCallibration = Vector3.zero;

    // Raw values
    [SerializeField] private int _rawPotentiometerValue;
    public int RawPotentiometerValue => _rawPotentiometerValue;

    // Processed values
    [SerializeField] private float _twistValue;
    public float TwistValue => _twistValue;

    // writing data
    [SerializeField] private string _outData = "0,0,0\n";

    // reading data on seperate thread
    private Thread _readThread;
    private bool _keepReading = true;
    private string _latestData = "0,0,0\n";
    private object _dataLock = new object();

    // writing data
    private bool _shouldWriteData = false;

    // initialize
    private void Awake()
    {
        if (!_running)
            OpenStream();
    }

    // Update is called once per frame
    void Update()
    {
        SerialUpdate();
    }

    public void SetRGBLed(int r, int g, int b)
    {

        r = Math.Clamp((int)(r * _ledCallibration.x), 0, 255);
        g = Math.Clamp((int)(g * _ledCallibration.y), 0, 255);
        b = Math.Clamp((int)(b * _ledCallibration.z), 0, 255);

        _outData = ($"{r},{g},{b}\n");
        _shouldWriteData = true;
    }

    #region Serial port communication
    private void OpenStream()
    {
        // open communication with Arduino
        _running = true;
        _dataStream = new SerialPort(_serialPort, _baud);
        _dataStream.Open();

        // Start reading serial data on a background thread
        _readThread = new Thread(ReadSerial);
        _readThread.Start();
    }

    private void SerialUpdate()
    {
        // read latest values from Arduino
        string receivedString;
        lock (_dataLock)
        {
            receivedString = _latestData;
        }

        // process data
        ProcessData(receivedString);

        // write data back
        if (_shouldWriteData) WriteData();
    }

    void ReadSerial()
    {
        while (_keepReading)
        {
            try
            {
                // read latest data
                string line = _dataStream.ReadLine();
                lock (_dataLock)
                {
                    _latestData = line;
                }
            }
            catch (System.Exception)
            {
                // ignore timeout or disconnection errors
            }
        }
    }

    // writes whatever data class has back to the data stream
    private void WriteData()
    {
        _dataStream.Write(_outData);
        _shouldWriteData = false;
    }

    public void CleanUp()
    {
        // prevent double entry
        if (!_keepReading || !_running) return;
        _keepReading = false;
        _running = false;

        // close reading thread
        if (_readThread != null && _readThread.IsAlive)
        {
            try { _readThread.Join(100); } catch { }
        }

        // close serial port
        if (_dataStream != null && _dataStream.IsOpen)
        {
            try { _dataStream.Close(); } catch { }
        }

        Debug.Log("Serial connection closed safely.");
    }
    #endregion

    #region Data Processing
    private void ProcessData(string data)
    {
        try
        {
            string[] inputs = data.Split(",");
            if (inputs[0].Length > 0)
            {
                // set raw values
                _rawPotentiometerValue = int.Parse(data);

                // map values
                _twistValue = MapTwist(_rawPotentiometerValue);
            }
        }
        catch (System.Exception _)
        {
            // ignore errors
        }
    }

    // map pot val to -1 - 1
    private float MapTwist(float value)
    {
        float twist = (value / 511.5f) - 1;
        if (MathF.Abs(twist - _twistValue) > _minStep)
            return twist;
        return _twistValue;
    }
    #endregion

    private void OnApplicationQuit()
    {
        if (_instance == null) return;
        CleanUp();
    }

    private void OnDestroy()
    {
        if(_instance == null) return; 
        CleanUp();
    }
}
