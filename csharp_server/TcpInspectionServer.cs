using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace MFCServer1
{
    public class TcpInspectionServer
    {
        private readonly int _port;
        private readonly PythonTcpClient _python;
        private readonly DatabaseService _db;
        private TcpListener _listener;
        private bool _running;

        // 이미지 저장할 기본 디렉토리
        private readonly string _saveDir = @"C:\InspectImages";

        public TcpInspectionServer(int listenPort,
                                   PythonTcpClient pythonService,
                                   DatabaseService dbService)
        {
            _port = listenPort;
            _python = pythonService;
            _db = dbService;
        }

        public void StartSync()
        {
            Directory.CreateDirectory(_saveDir); // 폴더 없으면 만든다.

            _listener = new TcpListener(IPAddress.Any, _port);
            _listener.Start();
            _running = true;

            ServerMonitor.TcpListening = true;
            ServerMonitor.TcpPort = _port;

            Console.WriteLine("[TCP] Listening on " + _port);

            while (_running)
            {
                TcpClient client = _listener.AcceptTcpClient();
                ThreadPool.QueueUserWorkItem(HandleClient, client);
            }
        }

        private void HandleClient(object state)
        {
            TcpClient client = (TcpClient)state;
            NetworkStream ns = client.GetStream();

            string remoteIp = ((IPEndPoint)client.Client.RemoteEndPoint).Address.ToString();
            ServerMonitor.LastClient = remoteIp;
            ServerMonitor.LastClientTime = DateTime.Now;

            Console.WriteLine("[TCP] Client connected: " + remoteIp);

            try
            {
                // ---- 1) 클라가 보낸 TOP 이미지 수신 ----
                string savedTopPath = ReceiveOneImage(ns, "top");

                // ---- 2) 클라가 보낸 SIDE 이미지 수신 ----
                string savedSidePath = ReceiveOneImage(ns, "side");

                Console.WriteLine("[TCP] savedTopPath : " + savedTopPath);
                Console.WriteLine("[TCP] savedSidePath: " + savedSidePath);

                // ---- 3) 파이썬 YOLO 분석 호출 ----
                string pyResultJson;

                bool hasTop = !string.IsNullOrEmpty(savedTopPath);
                bool hasSide = !string.IsNullOrEmpty(savedSidePath);

                if (hasTop && hasSide)
                {
                    pyResultJson = _python.AnalyzeDualAsync(savedTopPath, savedSidePath).Result;
                }
                else if (hasTop)
                {
                    pyResultJson = _python.AnalyzeSingleAsync(savedTopPath, "top").Result;
                }
                else if (hasSide)
                {
                    pyResultJson = _python.AnalyzeSingleAsync(savedSidePath, "side").Result;
                }
                else
                {
                    pyResultJson = "{\"result\":\"비정상\",\"error\":\"no image\"}";
                }

                Console.WriteLine("[TCP] PY RESULT: " + pyResultJson);

                // ---- 4) 결과 파싱 ----
                string finalResult = ExtractResult(pyResultJson);
                string reason = ExtractReason(pyResultJson);

                // ---- 5) 모니터/UI 갱신 ----
                ServerMonitor.LastResult = finalResult;
                ServerMonitor.LastTopImagePath = savedTopPath;
                ServerMonitor.LastSideImagePath = savedSidePath;

                ServerMonitor.AddLog(
                    finalResult,
                    reason,
                    savedTopPath,
                    savedSidePath
                );

                // ---- 6) (선택) DB 저장 ----
                // _db.InsertInspectionFull(...);

                // ---- 7) 클라이언트에게 최종 결과 보내기 ----
                byte[] ok = Encoding.UTF8.GetBytes(finalResult);
                ns.Write(ok, 0, ok.Length);
            }
            catch (Exception ex)
            {
                Console.WriteLine("[TCP] HandleClient error: " + ex.Message);
            }
            finally
            {
                ns.Close();
                client.Close();
            }
        }

        // 클라가 보낸 이미지 1장을 받아서 디스크에 저장하고, 저장된 경로 리턴
        // 없으면 "" 리턴
        private string ReceiveOneImage(NetworkStream ns, string camLabel)
        {
            // 1바이트: 이 카메라 이미지가 있는지 여부 (0x00 or 0x01)
            int hasFlag = ns.ReadByte();
            if (hasFlag == -1) throw new IOException("stream closed unexpectedly");

            if (hasFlag == 0x00)
            {
                // 이미지 없음
                return "";
            }

            // 이미지 있음(0x01)

            // (1) 파일명 길이 읽기 (4바이트 int)
            byte[] lenNameBuf = ReadExact(ns, 4);
            int nameLen = BitConverter.ToInt32(lenNameBuf, 0);

            // (2) 파일명 읽기
            byte[] nameBuf = ReadExact(ns, nameLen);
            string origName = Encoding.UTF8.GetString(nameBuf); // 원래 파일명 (ex: "1.jpg")

            // (3) 이미지 데이터 길이 읽기 (4바이트 int)
            byte[] lenImgBuf = ReadExact(ns, 4);
            int imgLen = BitConverter.ToInt32(lenImgBuf, 0);

            // (4) 이미지 데이터 읽기
            byte[] imgBuf = ReadExact(ns, imgLen);

            // 저장 경로 만들기 (타임스탬프+camLabel)
            string timeTag = DateTime.Now.ToString("yyyyMMdd_HHmmss_fff");
            string ext = Path.GetExtension(origName);
            string saveName = $"{timeTag}_{camLabel}{ext}";
            string fullPath = Path.Combine(_saveDir, saveName);

            File.WriteAllBytes(fullPath, imgBuf); // 디스크에 저장

            return fullPath; // 나중에 파이썬 분석 / UI 표시용
        }

        // 정확히 count바이트 다 받을 때까지 blocking으로 읽는 헬퍼
        private byte[] ReadExact(NetworkStream ns, int count)
        {
            byte[] buf = new byte[count];
            int offset = 0;
            while (offset < count)
            {
                int n = ns.Read(buf, offset, count - offset);
                if (n <= 0)
                    throw new IOException("stream ended early");
                offset += n;
            }
            return buf;
        }

        // 아래 ExtractResult / ExtractReason 은 기존 코드 그대로 사용하면 됨
        private string ExtractResult(string json) { /* ... */ return "정상"; }
        private string ExtractReason(string json) { /* ... */ return ""; }
    }
}
