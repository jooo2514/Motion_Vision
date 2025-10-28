#include "pch.h"
#include "framework.h"
#include "CanClient.h"
#include "CanClientDlg.h"
#include "afxdialogex.h"

#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace Pylon;
using namespace cv;

void CCanClientDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

// ===================== 도우미 함수 =====================
void CCanClientDlg::DrawMatToCtrl(const Mat& img, CWnd* pWnd)
{
    if (!pWnd || img.empty()) return;
    CClientDC dc(pWnd);
    CRect rc; pWnd->GetClientRect(&rc);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = img.cols;
    bmi.bmiHeader.biHeight = -img.rows; // 위→아래 순서
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(dc.GetSafeHdc(),
        0, 0, rc.Width(), rc.Height(),
        0, 0, img.cols, img.rows,
        img.data, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

// ===================== TCP 전송 =====================
bool CCanClientDlg::SendImageToServer(const std::string& imgPath)
{
    std::ifstream file(imgPath, std::ios::binary | std::ios::ate);
    if (!file) return false;
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size)) return false;

    WSADATA wsa;
    SOCKET sock = INVALID_SOCKET;
    sockaddr_in servAddr{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(9000);
    inet_pton(AF_INET, "127.0.0.1", &servAddr.sin_addr);

    if (connect(sock, (SOCKADDR*)&servAddr, sizeof(servAddr)) == SOCKET_ERROR)
    {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    int len = static_cast<int>(size);
    send(sock, (char*)&len, sizeof(int), 0);
    send(sock, buffer.data(), len, 0);

    char resp[256] = { 0 };
    int recvLen = recv(sock, resp, sizeof(resp) - 1, 0);
    if (recvLen > 0)
    {
        resp[recvLen] = 0;
        CString msg;
        msg.Format(L"서버 응답: %S", resp);
        SetDlgItemText(IDC_STATIC_STATUS, msg);
    }

    closesocket(sock);
    WSACleanup();
    return true;
}

// ===================== 메시지 맵 =====================
BEGIN_MESSAGE_MAP(CCanClientDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_START, &CCanClientDlg::OnBnClickedBtnStart)
    ON_WM_DESTROY()
    ON_WM_TIMER()
END_MESSAGE_MAP()

// ===================== 생성자 =====================
CCanClientDlg::CCanClientDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_CANCLIENT_DIALOG, pParent)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

// ===================== OnInitDialog =====================
BOOL CCanClientDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 연결 중..."));

    try {
        PylonInitialize();

        CTlFactory& factory = CTlFactory::GetInstance();
        DeviceInfoList_t devices;
        if (factory.EnumerateDevices(devices) < 2) {
            AfxMessageBox(L"Basler 카메라 2대를 모두 연결해야 합니다.");
            return TRUE;
        }

        m_camTop.Attach(factory.CreateDevice(devices[0]));
        m_camFront.Attach(factory.CreateDevice(devices[1]));

        m_camTop.Open();
        m_camFront.Open();

        // ⚡ 여기서 한 번만 StartGrabbing
        m_camTop.StartGrabbing(GrabStrategy_LatestImageOnly);
        m_camFront.StartGrabbing(GrabStrategy_LatestImageOnly);

        // 타이머 시작
        m_timerId = SetTimer(1, 33, nullptr); // 약 30fps
        SetDlgItemText(IDC_STATIC_STATUS, _T("카메라 2대 연결 완료"));
    }
    catch (const GenericException& e) {
        CString msg(e.GetDescription());
        AfxMessageBox(msg);
    }

    return TRUE;
}


// ===================== OnTimer: 실시간 표시 =====================
void CCanClientDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1)
    {
        try {
            CGrabResultPtr grabTop, grabFront;

            if (m_camTop.IsGrabbing() &&
                m_camTop.RetrieveResult(50, grabTop, TimeoutHandling_Return) &&
                grabTop->GrabSucceeded())
            {
                m_converter.OutputPixelFormat = PixelType_BGR8packed;
                m_converter.Convert(m_pylonImage, grabTop);
                Mat img((int)grabTop->GetHeight(), (int)grabTop->GetWidth(),
                    CV_8UC3, (void*)m_pylonImage.GetBuffer());
                DrawMatToCtrl(img, GetDlgItem(IDC_CAM_TOP));
            }

            if (m_camFront.IsGrabbing() &&
                m_camFront.RetrieveResult(50, grabFront, TimeoutHandling_Return) &&
                grabFront->GrabSucceeded())
            {
                m_converter.OutputPixelFormat = PixelType_BGR8packed;
                m_converter.Convert(m_pylonImage, grabFront);
                Mat img((int)grabFront->GetHeight(), (int)grabFront->GetWidth(),
                    CV_8UC3, (void*)m_pylonImage.GetBuffer());
                DrawMatToCtrl(img, GetDlgItem(IDC_CAM_FRONT));
            }
        }
        catch (...) {
            OutputDebugString(L"[Basler] RetrieveResult error\n");
        }
    }
    CDialogEx::OnTimer(nIDEvent);
}

// ===================== 촬영 및 전송 버튼 =====================
void CCanClientDlg::OnBnClickedBtnStart()
{
    GetDlgItem(IDC_BTN_START)->EnableWindow(FALSE);
    SetDlgItemText(IDC_STATIC_STATUS, _T("이미지 캡처 중..."));

    try {
        CString folder = L"C:\\CanClient\\captures";
        CreateDirectory(folder, NULL);

        // ============================
        // (1) 카메라 상태 초기화
        // ============================
        if (!m_camTop.IsOpen()) m_camTop.Open();
        if (!m_camFront.IsOpen()) m_camFront.Open();

        if (!m_camTop.IsGrabbing())
            m_camTop.StartGrabbing(GrabStrategy_LatestImageOnly);
        if (!m_camFront.IsGrabbing())
            m_camFront.StartGrabbing(GrabStrategy_LatestImageOnly);

        Sleep(100); // 안정화 대기

        CGrabResultPtr grabTop, grabFront;

        // ============================
        // (2) 상단 카메라 캡처
        // ============================
        if (m_camTop.IsGrabbing()) {
            // 정면 일시 정지 (대역폭 확보)
            if (m_camFront.IsGrabbing()) m_camFront.StopGrabbing();
            Sleep(100);

            if (m_camTop.RetrieveResult(800, grabTop, TimeoutHandling_Return) && grabTop->GrabSucceeded()) {
                m_converter.OutputPixelFormat = PixelType_BGR8packed;
                CPylonImage imgTop;
                m_converter.Convert(imgTop, grabTop);
                Mat topMat((int)grabTop->GetHeight(), (int)grabTop->GetWidth(),
                    CV_8UC3, (void*)imgTop.GetBuffer());

                std::string topPath = "C:\\CanClient\\captures\\capture_" +
                    std::to_string(time(NULL)) + "_" +
                    std::to_string(GetTickCount64() % 1000) + "_top.jpg";

                if (imwrite(topPath, topMat))
                    SendImageToServer(topPath);
                else
                    AfxMessageBox(L"상단 이미지 저장 실패");
            }

            // 정면 다시 실행
            m_camFront.StartGrabbing(GrabStrategy_LatestImageOnly);
        }

        // ============================
        // (3) 정면 카메라 캡처
        // ============================
        if (m_camFront.IsGrabbing()) {
            Sleep(100);
            if (m_camFront.RetrieveResult(800, grabFront, TimeoutHandling_Return) && grabFront->GrabSucceeded()) {
                m_converter.OutputPixelFormat = PixelType_BGR8packed;
                CPylonImage imgFront;
                m_converter.Convert(imgFront, grabFront);
                Mat frontMat((int)grabFront->GetHeight(), (int)grabFront->GetWidth(),
                    CV_8UC3, (void*)imgFront.GetBuffer());

                std::string frontPath = "C:\\CanClient\\captures\\capture_" +
                    std::to_string(time(NULL)) + "_" +
                    std::to_string(GetTickCount64() % 1000) + "_front.jpg";

                if (imwrite(frontPath, frontMat))
                    SendImageToServer(frontPath);
                else
                    AfxMessageBox(L"정면 이미지 저장 실패");
            }
        }

        // ============================
        // (4) 카메라 버퍼 정리 및 상태 유지
        // ============================
        m_camTop.StopGrabbing();
        m_camFront.StopGrabbing();

        // 다음 프레임을 위해 다시 Grab 재시작
        m_camTop.StartGrabbing(GrabStrategy_LatestImageOnly);
        m_camFront.StartGrabbing(GrabStrategy_LatestImageOnly);

        SetDlgItemText(IDC_STATIC_STATUS, _T("상단/정면 촬영 및 전송 완료 ✅"));
    }
    catch (const GenericException& e) {
        CString msg(e.GetDescription());
        AfxMessageBox(msg);
    }
    catch (const std::exception& e) {
        CString msg(e.what());
        AfxMessageBox(msg);
    }

    GetDlgItem(IDC_BTN_START)->EnableWindow(TRUE);
}



// ===================== 종료 =====================
void CCanClientDlg::OnDestroy()
{
    CDialogEx::OnDestroy();
    if (m_timerId) { KillTimer(m_timerId); m_timerId = 0; }

    try {
        if (m_camTop.IsGrabbing()) m_camTop.StopGrabbing();
        if (m_camTop.IsOpen()) m_camTop.Close();
        if (m_camFront.IsGrabbing()) m_camFront.StopGrabbing();
        if (m_camFront.IsOpen()) m_camFront.Close();
        PylonTerminate();
    }
    catch (...) {}
}
