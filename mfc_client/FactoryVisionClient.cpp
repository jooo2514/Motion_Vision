// FactoryVisionClient.cpp: 애플리케이션에 대한 클래스 동작 정의

#include "pch.h"
#include "framework.h"
#include "afxwinappex.h"
#include "afxdialogex.h"
#include "FactoryVisionClient.h"
#include "MainFrm.h"
#include "FactoryVisionClientDoc.h"
#include "FactoryVisionClientView.h"
#include <afxdisp.h> // IDP_OLE_INIT_FAILED 사용 위해 포함 (보통 pch.h에 이미 있음)
#include <gdiplus.h> // GdiplusStartup/Shutdown 사용 위해 포함 (보통 pch.h에 이미 있음)
#pragma comment(lib, "gdiplus.lib") // GDI+ 라이브러리 링크

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CFactoryVisionClientApp, CWinApp)
    ON_COMMAND(ID_APP_ABOUT, &CFactoryVisionClientApp::OnAppAbout)
    ON_COMMAND(ID_FILE_NEW, &CWinApp::OnFileNew)
    ON_COMMAND(ID_FILE_OPEN, &CWinApp::OnFileOpen)
    ON_COMMAND(ID_FILE_PRINT_SETUP, &CWinApp::OnFilePrintSetup)
END_MESSAGE_MAP()

CFactoryVisionClientApp theApp;

CFactoryVisionClientApp::CFactoryVisionClientApp() noexcept
    : m_gdiplusToken(0)
{
    m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_ALL_ASPECTS;
    SetAppID(_T("MyCompany.FactoryVisionClient.VisionClient.1.0"));
}

BOOL CFactoryVisionClientApp::InitInstance()
{
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    CWinApp::InitInstance();

    // OLE 라이브러리를 초기화합니다.
    if (!AfxOleInit())
    {
        // --- 수정된 부분 (IDP_OLE_INIT_FAILED 정의 확인) ---
        // Resource.h에 IDP_OLE_INIT_FAILED 정의가 있는지 확인 필요.
        // 일반적으로 MFC 프로젝트 생성 시 자동으로 추가됩니다.
        AfxMessageBox(IDP_OLE_INIT_FAILED);
        // --- 수정 끝 ---
        return FALSE;
    }
    AfxEnableControlContainer();
    EnableTaskbarInteraction(FALSE);

    // GDI+ 초기화
    Gdiplus::GdiplusStartupInput gsi;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gsi, NULL) != Gdiplus::Ok)
    {
        AfxMessageBox(_T("GDI+ 초기화 실패")); return FALSE;
    }

    // Winsock 초기화
    if (!InitializeWinsock())
    {
        AfxMessageBox(_T("Winsock 초기화 실패"));
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        return FALSE;
    }

    SetRegistryKey(_T("MyCompany Vision Systems"));
    LoadStdProfileSettings(4);

    CSingleDocTemplate* pDocTemplate = new CSingleDocTemplate(
        IDR_MAINFRAME,
        RUNTIME_CLASS(CFactoryVisionClientDoc),
        RUNTIME_CLASS(CMainFrame),
        RUNTIME_CLASS(CFactoryVisionClientView));
    if (!pDocTemplate)
    {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        WSACleanup();
        return FALSE;
    }
    AddDocTemplate(pDocTemplate);

    CCommandLineInfo cmdInfo; ParseCommandLine(cmdInfo);
    if (!ProcessShellCommand(cmdInfo))
    {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        WSACleanup();
        return FALSE;
    }

    m_pMainWnd->ShowWindow(SW_SHOWMAXIMIZED);
    m_pMainWnd->UpdateWindow();
    return TRUE;
}

int CFactoryVisionClientApp::ExitInstance()
{
    AfxOleTerm(FALSE);
    if (m_gdiplusToken) Gdiplus::GdiplusShutdown(m_gdiplusToken);
    WSACleanup();
    return CWinApp::ExitInstance();
}

BOOL CFactoryVisionClientApp::InitializeWinsock()
{
    WSADATA wsa; return (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
}

// CAboutDlg 대화 상자 정의 (변경 없음)
class CAboutDlg : public CDialogEx
{
public: CAboutDlg() noexcept : CDialogEx(IDD_ABOUTBOX) {}
protected:
    virtual void DoDataExchange(CDataExchange* pDX) { CDialogEx::DoDataExchange(pDX); }
    DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx) END_MESSAGE_MAP()

    void CFactoryVisionClientApp::OnAppAbout()
{
    CAboutDlg dlg; dlg.DoModal();
}