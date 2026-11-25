using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

internal static class CoolingCanOta
{
    private const string ControlCanDirectory = @"C:\Program Files (x86)\USB_CAN TOOL";
    private const string ControlCanDll = ControlCanDirectory + @"\ControlCAN.dll";

    private const uint DeviceType = 4;
    private const uint DeviceIndex = 0;
    private const uint CanIndex = 0;

    private const uint IdPowerBroadcast = 0x04001000;
    private const uint IdHandshakeRequest = 0x04402000;
    private const uint IdHandshakeReply = 0x08202100;
    private const uint IdOtaRequest = 0x04402210;
    private const uint IdOtaRequestReply = 0x08202210;
    private const uint IdOtaDataBase = 0x04410000;
    private const uint IdOtaDataReplyBase = 0x08210000;
    private const uint IdOtaComplete = 0x08202220;

    private const int RblHeaderSize = 96;
    private const int DownloadPartitionSize = 0x1A800;
    private const int PrepareTimeoutMs = 10000;
    private const int DataAckTimeoutMs = 180;
    private const int DataRetryCount = 3;
    private const int CompleteTimeoutMs = 20000;

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct VciInitConfig
    {
        public uint AccCode;
        public uint AccMask;
        public uint Reserved;
        public byte Filter;
        public byte Timing0;
        public byte Timing1;
        public byte Mode;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    private struct VciCanObj
    {
        public uint ID;
        public uint TimeStamp;
        public byte TimeFlag;
        public byte SendType;
        public byte RemoteFlag;
        public byte ExternFlag;
        public byte DataLen;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
        public byte[] Data;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
        public byte[] Reserved;
    }

    private static class Native
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern bool SetDllDirectory(string path);

        [DllImport("ControlCAN.dll", CallingConvention = CallingConvention.StdCall)]
        internal static extern uint VCI_OpenDevice(uint deviceType, uint deviceIndex,
                                                   uint reserved);

        [DllImport("ControlCAN.dll", CallingConvention = CallingConvention.StdCall)]
        internal static extern uint VCI_CloseDevice(uint deviceType, uint deviceIndex);

        [DllImport("ControlCAN.dll", CallingConvention = CallingConvention.StdCall)]
        internal static extern uint VCI_InitCAN(uint deviceType, uint deviceIndex,
                                                uint canIndex,
                                                ref VciInitConfig config);

        [DllImport("ControlCAN.dll", CallingConvention = CallingConvention.StdCall)]
        internal static extern uint VCI_ClearBuffer(uint deviceType, uint deviceIndex,
                                                    uint canIndex);

        [DllImport("ControlCAN.dll", CallingConvention = CallingConvention.StdCall)]
        internal static extern uint VCI_StartCAN(uint deviceType, uint deviceIndex,
                                                 uint canIndex);

        [DllImport("ControlCAN.dll", CallingConvention = CallingConvention.StdCall)]
        internal static extern uint VCI_ResetCAN(uint deviceType, uint deviceIndex,
                                                 uint canIndex);

        [DllImport("ControlCAN.dll", CallingConvention = CallingConvention.StdCall)]
        internal static extern uint VCI_Transmit(uint deviceType, uint deviceIndex,
                                                 uint canIndex,
                                                 ref VciCanObj frame,
                                                 uint length);

        [DllImport("ControlCAN.dll", CallingConvention = CallingConvention.StdCall)]
        internal static extern uint VCI_Receive(uint deviceType, uint deviceIndex,
                                                uint canIndex, IntPtr frames,
                                                uint length, int waitTime);
    }

    private sealed class CanDevice : IDisposable
    {
        private readonly object nativeLock = new object();
        private readonly object receiveLock = new object();
        private readonly Queue<VciCanObj> receiveQueue = new Queue<VciCanObj>();
        private readonly AutoResetEvent receiveEvent = new AutoResetEvent(false);
        private Thread receiveThread;
        private volatile bool receiveRunning;
        private volatile bool receiveFailed;
        private bool opened;

        public void Open()
        {
            VciInitConfig config = new VciInitConfig
            {
                AccCode = 0,
                AccMask = 0xFFFFFFFF,
                Reserved = 0,
                Filter = 1,
                Timing0 = 0x00,
                Timing1 = 0x1C,
                Mode = 0
            };

            if (!File.Exists(ControlCanDll))
            {
                throw new InvalidOperationException("找不到 " + ControlCanDll);
            }
            if (!Native.SetDllDirectory(ControlCanDirectory))
            {
                throw new InvalidOperationException("无法设置 ControlCAN.dll 搜索目录");
            }
            if (Native.VCI_OpenDevice(DeviceType, DeviceIndex, 0) != 1)
            {
                throw new InvalidOperationException(
                    "打开USBCAN-II失败。请关闭 USB-CAN Tool，并确认设备已连接。");
            }
            opened = true;

            if (Native.VCI_InitCAN(DeviceType, DeviceIndex, CanIndex, ref config) != 1)
            {
                throw new InvalidOperationException("初始化CAN1失败");
            }
            if (Native.VCI_ClearBuffer(DeviceType, DeviceIndex, CanIndex) != 1)
            {
                throw new InvalidOperationException("清空CAN1缓冲区失败");
            }
            if (Native.VCI_StartCAN(DeviceType, DeviceIndex, CanIndex) != 1)
            {
                throw new InvalidOperationException("启动CAN1失败");
            }
            Thread.Sleep(100);
            StartReceiver();
        }

        public void Send(uint id, byte[] data, int offset, int length)
        {
            VciCanObj frame = NewFrame();
            frame.ID = id;
            frame.SendType = 0;
            frame.RemoteFlag = 0;
            frame.ExternFlag = 1;
            frame.DataLen = (byte)length;
            Buffer.BlockCopy(data, offset, frame.Data, 0, length);

            uint sent;
            lock (nativeLock)
            {
                sent = Native.VCI_Transmit(DeviceType, DeviceIndex, CanIndex,
                                           ref frame, 1);
            }
            if (sent != 1)
            {
                throw new InvalidOperationException(
                    string.Format("CAN发送失败，ID=0x{0:X8}", id));
            }
        }

        public VciCanObj WaitFor(uint expectedId, int timeoutMs)
        {
            Stopwatch timer = Stopwatch.StartNew();
            Dictionary<uint, int> unexpectedFrames = new Dictionary<uint, int>();

            while (timer.ElapsedMilliseconds < timeoutMs)
            {
                VciCanObj frame;
                bool haveFrame = false;

                if (receiveFailed)
                {
                    throw new InvalidOperationException("读取USBCAN-II失败或设备掉线");
                }

                lock (receiveLock)
                {
                    if (receiveQueue.Count != 0)
                    {
                        frame = receiveQueue.Dequeue();
                        haveFrame = true;
                    }
                    else
                    {
                        frame = NewFrame();
                    }
                }

                if (!haveFrame)
                {
                    int remaining = timeoutMs - (int)timer.ElapsedMilliseconds;
                    receiveEvent.WaitOne(Math.Max(1, Math.Min(remaining, 20)));
                    continue;
                }

                if ((frame.ExternFlag == 1) && (frame.RemoteFlag == 0) &&
                    (frame.ID == expectedId))
                {
                    return frame;
                }

                int count;
                unexpectedFrames.TryGetValue(frame.ID, out count);
                unexpectedFrames[frame.ID] = count + 1;

                if ((expectedId != IdOtaComplete) &&
                    (frame.ExternFlag == 1) && (frame.ID == IdOtaComplete) &&
                    (frame.DataLen >= 2))
                {
                    throw new InvalidOperationException(
                        "板端提前结束OTA：" + CompleteResultText(frame));
                }
            }

            StringBuilder details = new StringBuilder();
            foreach (KeyValuePair<uint, int> item in unexpectedFrames)
            {
                if (details.Length != 0)
                {
                    details.Append(", ");
                }
                details.AppendFormat("0x{0:X8} x{1}", item.Key, item.Value);
            }

            throw new TimeoutException(string.Format(
                "等待ID 0x{0:X8}超时；期间收到：{1}", expectedId,
                details.Length == 0 ? "无其他CAN帧" : details.ToString()));
        }

        public void ClearBuffer()
        {
            lock (nativeLock)
            {
                Native.VCI_ClearBuffer(DeviceType, DeviceIndex, CanIndex);
            }
            lock (receiveLock)
            {
                receiveQueue.Clear();
            }
        }

        public void RestartChannel()
        {
            lock (nativeLock)
            {
                if (Native.VCI_ResetCAN(DeviceType, DeviceIndex, CanIndex) != 1)
                {
                    throw new InvalidOperationException("复位CAN1失败");
                }
                Thread.Sleep(20);
                if (Native.VCI_ClearBuffer(DeviceType, DeviceIndex, CanIndex) != 1)
                {
                    throw new InvalidOperationException("复位后清空CAN1缓冲区失败");
                }
                if (Native.VCI_StartCAN(DeviceType, DeviceIndex, CanIndex) != 1)
                {
                    throw new InvalidOperationException("复位后重新启动CAN1失败");
                }
            }
            lock (receiveLock)
            {
                receiveQueue.Clear();
            }
            receiveFailed = false;
            Thread.Sleep(100);
        }

        private void StartReceiver()
        {
            receiveFailed = false;
            receiveRunning = true;
            receiveThread = new Thread(ReceiveLoop);
            receiveThread.IsBackground = true;
            receiveThread.Name = "usbc_rx";
            receiveThread.Start();
        }

        private void ReceiveLoop()
        {
            const int receiveCapacity = 100;
            int frameSize = Marshal.SizeOf(typeof(VciCanObj));
            IntPtr receiveBuffer = Marshal.AllocHGlobal(frameSize * receiveCapacity);

            try
            {
                while (receiveRunning)
                {
                    uint received;

                    lock (nativeLock)
                    {
                        received = Native.VCI_Receive(DeviceType, DeviceIndex,
                                                      CanIndex, receiveBuffer,
                                                      receiveCapacity, 0);
                    }

                    if (received == 0xFFFFFFFF)
                    {
                        receiveFailed = true;
                        receiveEvent.Set();
                        return;
                    }
                    if (received == 0)
                    {
                        Thread.Sleep(1);
                        continue;
                    }

                    lock (receiveLock)
                    {
                        for (uint index = 0; index < received; ++index)
                        {
                            IntPtr address = IntPtr.Add(receiveBuffer,
                                checked((int)index * frameSize));
                            VciCanObj frame = (VciCanObj)Marshal.PtrToStructure(
                                address, typeof(VciCanObj));
                            receiveQueue.Enqueue(frame);
                        }
                    }
                    receiveEvent.Set();
                }
            }
            finally
            {
                Marshal.FreeHGlobal(receiveBuffer);
            }
        }

        public void Dispose()
        {
            if (opened)
            {
                receiveRunning = false;
                receiveEvent.Set();
                if (receiveThread != null)
                {
                    receiveThread.Join(500);
                    receiveThread = null;
                }
                Native.VCI_CloseDevice(DeviceType, DeviceIndex);
                opened = false;
            }
        }
    }

    private static VciCanObj NewFrame()
    {
        return new VciCanObj
        {
            Data = new byte[8],
            Reserved = new byte[3]
        };
    }

    private static void PutU16(byte[] data, int offset, ushort value)
    {
        data[offset] = (byte)(value >> 8);
        data[offset + 1] = (byte)value;
    }

    private static void PutU32(byte[] data, int offset, uint value)
    {
        data[offset] = (byte)(value >> 24);
        data[offset + 1] = (byte)(value >> 16);
        data[offset + 2] = (byte)(value >> 8);
        data[offset + 3] = (byte)value;
    }

    private static ushort GetU16(byte[] data, int offset)
    {
        return (ushort)((data[offset] << 8) | data[offset + 1]);
    }

    private static ushort Crc16CcittFalse(byte[] data)
    {
        ushort crc = 0xFFFF;

        foreach (byte value in data)
        {
            crc ^= (ushort)(value << 8);
            for (int bit = 0; bit < 8; ++bit)
            {
                crc = (crc & 0x8000) != 0
                    ? (ushort)((crc << 1) ^ 0x1021)
                    : (ushort)(crc << 1);
            }
        }
        return crc;
    }

    private static void SendPowerOff(CanDevice can)
    {
        can.Send(IdPowerBroadcast, new byte[4], 0, 4);
        Thread.Sleep(50);
    }

    private static void ProbeApplication(CanDevice can, int timeoutMs)
    {
        byte[] request = new byte[8];
        can.Send(IdHandshakeRequest, request, 0, request.Length);
        VciCanObj reply = can.WaitFor(IdHandshakeReply, timeoutMs);

        if (reply.DataLen != 8)
        {
            throw new InvalidOperationException("握手回复DLC不是8");
        }

        Console.WriteLine("App在线：设备={0}，协议={1}，软件={2}，硬件={3}",
                          GetU16(reply.Data, 0), GetU16(reply.Data, 2),
                          GetU16(reply.Data, 4), GetU16(reply.Data, 6));
    }

    private static void RunSafeTest(CanDevice can)
    {
        byte[] request = new byte[8];
        PutU16(request, 0, 1);

        Console.WriteLine("发送无效长度OTA请求，预期板端安全拒绝...");
        Console.WriteLine("TX 0x04402210 DLC8  00 01 00 00 00 00 00 00");
        can.Send(IdOtaRequest, request, 0, request.Length);
        VciCanObj reply = can.WaitFor(IdOtaRequestReply, 5000);

        if ((reply.DataLen == 2) && (reply.Data[0] == 0))
        {
            Console.WriteLine("OTA入口测试通过：收到 0x08202210 / 00 00，未进入擦写。");
            return;
        }
        throw new InvalidOperationException("OTA入口没有按预期拒绝无效请求");
    }

    private static void ValidateRbl(byte[] rbl)
    {
        if ((rbl.Length < RblHeaderSize) ||
            (rbl.Length > DownloadPartitionSize))
        {
            throw new InvalidOperationException(
                string.Format("RBL长度{0}超出允许范围 {1}..{2}",
                              rbl.Length, RblHeaderSize, DownloadPartitionSize));
        }
        if ((rbl[0] != (byte)'R') || (rbl[1] != (byte)'B') ||
            (rbl[2] != (byte)'L') || (rbl[3] != 0))
        {
            throw new InvalidOperationException("文件不是标准RBL：头部缺少 RBL\\0");
        }
        if ((rbl[4] != 0) || (rbl[5] != 0))
        {
            throw new InvalidOperationException("当前板端只接受不压缩、不加密的RBL");
        }

        string partition = Encoding.ASCII.GetString(rbl, 12, 16).TrimEnd('\0');
        if (partition != "app")
        {
            throw new InvalidOperationException(
                "RBL目标分区必须是小写app，当前为：" + partition);
        }
    }

    private static void RunUpgrade(CanDevice can, string rblPath)
    {
        byte[] rbl = File.ReadAllBytes(rblPath);
        ValidateRbl(rbl);

        ushort crc16 = Crc16CcittFalse(rbl);
        byte[] request = new byte[8];
        PutU16(request, 0, 1);
        PutU16(request, 2, crc16);
        PutU32(request, 4, (uint)rbl.Length);

        int frameCount = (rbl.Length + 7) / 8;
        Console.WriteLine("RBL：{0}", Path.GetFullPath(rblPath));
        Console.WriteLine("长度：{0} 字节，CRC16=0x{1:X4}，数据帧={2}",
                          rbl.Length, crc16, frameCount);

        SendPowerOff(can);
        Console.WriteLine("已发送系统断电命令，准备擦除下载分区...");
        Console.WriteLine("TX 0x04402210 DLC8  {0}",
                          BitConverter.ToString(request).Replace('-', ' '));
        can.Send(IdOtaRequest, request, 0, request.Length);

        VciCanObj prepareReply = can.WaitFor(IdOtaRequestReply,
                                             PrepareTimeoutMs);
        if ((prepareReply.DataLen != 2) || (prepareReply.Data[0] != 1))
        {
            throw new InvalidOperationException("板端拒绝OTA准备请求");
        }
        Console.WriteLine("下载分区擦除完成，开始传输...");

        int lastPercent = -1;
        for (int sequence = 0; sequence < frameCount; ++sequence)
        {
            int offset = sequence * 8;
            int length = Math.Min(8, rbl.Length - offset);
            uint sendId = IdOtaDataBase | (uint)sequence;
            uint replyId = IdOtaDataReplyBase | (uint)sequence;
            bool acknowledged = false;

            for (int attempt = 1; attempt <= DataRetryCount; ++attempt)
            {
                can.Send(sendId, rbl, offset, length);
                try
                {
                    VciCanObj reply = can.WaitFor(replyId, DataAckTimeoutMs);
                    if ((reply.DataLen == 2) && (reply.Data[0] == 1))
                    {
                        acknowledged = true;
                        break;
                    }
                    throw new InvalidOperationException(
                        string.Format("序号{0}被板端拒绝", sequence));
                }
                catch (TimeoutException)
                {
                    if (attempt == DataRetryCount)
                    {
                        throw;
                    }
                }
            }

            if (!acknowledged)
            {
                throw new InvalidOperationException(
                    string.Format("序号{0}没有收到成功ACK", sequence));
            }

            int percent = (sequence + 1) * 100 / frameCount;
            if (percent != lastPercent)
            {
                Console.Write("\r传输进度：{0,3}%  ({1}/{2})", percent,
                              sequence + 1, frameCount);
                lastPercent = percent;
            }
        }

        Console.WriteLine("\n数据发送完成，等待整包与RBL校验...");
        VciCanObj complete = can.WaitFor(IdOtaComplete, CompleteTimeoutMs);
        if ((complete.DataLen != 4) || (complete.Data[0] != 1))
        {
            throw new InvalidOperationException(
                "OTA校验失败：" + CompleteResultText(complete));
        }

        Console.WriteLine("板端校验通过，即将复位并由Bootloader安装新App。");
        Console.WriteLine("等待新App启动并读取握手版本...");
        WaitForNewApplication(can);
    }

    private static void WaitForNewApplication(CanDevice can)
    {
        Stopwatch timer = Stopwatch.StartNew();
        Thread.Sleep(3000);

        /* MCU 复位和 Bootloader 擦写期间总线上可能暂时没有节点应答，
         * 先恢复一次 USB-CAN 通道，避免累计 ACK 错误影响后续握手。 */
        can.RestartChannel();

        while (timer.ElapsedMilliseconds < 30000)
        {
            try
            {
                ProbeApplication(can, 500);
                Console.WriteLine("OTA完整流程通过。");
                return;
            }
            catch (TimeoutException)
            {
                can.RestartChannel();
                Thread.Sleep(250);
            }
        }
        throw new TimeoutException(
            "升级完成后30秒内没有收到新App握手，请检查Bootloader安装状态");
    }

    private static string CompleteResultText(VciCanObj frame)
    {
        if ((frame.Data == null) || (frame.DataLen < 2))
        {
            return "完成帧格式错误";
        }
        if (frame.Data[0] == 1)
        {
            return "成功";
        }

        switch (frame.Data[1])
        {
            case 0: return "CRC16错误";
            case 1: return "帧间超时";
            case 2: return "RBL、Flash或元数据校验错误";
            default: return "未知失败原因 " + frame.Data[1];
        }
    }

    private static void PrintUsage()
    {
        Console.WriteLine("创芯科技USBCAN-II / CoolingCtrlBoard CAN OTA工具");
        Console.WriteLine();
        Console.WriteLine("使用前必须关闭 USB-CAN Tool V9.11，设备同一时间只能被一个程序打开。");
        Console.WriteLine();
        Console.WriteLine("安全测试：CoolingCanOta.exe test");
        Console.WriteLine("完整升级：CoolingCanOta.exe <cooling_app.rbl>");
    }

    public static int Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;

        if (args.Length != 1)
        {
            PrintUsage();
            return 2;
        }

        try
        {
            using (CanDevice can = new CanDevice())
            {
                Console.WriteLine("打开USBCAN-II：设备0、CAN1、500 kbit/s、正常模式");
                can.Open();

                if (string.Equals(args[0], "test",
                                  StringComparison.OrdinalIgnoreCase))
                {
                    RunSafeTest(can);
                }
                else
                {
                    if (!File.Exists(args[0]))
                    {
                        throw new FileNotFoundException("找不到RBL文件", args[0]);
                    }
                    RunUpgrade(can, args[0]);
                }
            }
            return 0;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine("失败：" + error.Message);
            return 1;
        }
    }
}
