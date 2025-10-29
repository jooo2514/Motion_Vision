#include "pch.h"
#include "MainFrm.h"
#include "FactoryVisionClient.h"
#include "FactoryVisionClientView.h"
#include "SharedData.h"
#include "resource.h"
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_MESSAGE(WM_TCP_STATUS, &CMainFrame::OnTcpStatus)
    ON_MESSAGE(WM_CAMERA_DISCONNECTED, &CMainFrame::OnCameraDisconnected)
    ON_MESSAGE(WM_CAMERA_STATUS, &CMainFrame::OnCameraStatus)
    ON_COMMAND_RANGE(ID_CAMERA_BTN_BASE, ID_CAMERA_BTN_BASE + 63, &CMainFrame::OnCameraViewClick)
    ON_COMMAND(ID_MANUAL_CAPTURE_BTN, &CMainFrame::OnManualCaptureClick)
    ON_COMMAND(ID_SETTINGS_BTN, &CMainFrame::OnSettingsClick)

    ON_COMMAND(ID_VIEW_TOGGLE_LIVEPANEL, &CMainFrame::OnTogglePanel)
    ON_COMMAND(ID_VIEW_DOCK_LEFT, &CMainFrame::OnDockLeft)
    ON_COMMAND(ID_VIEW_DOCK_RIGHT, &CMainFrame::OnDockRight)
    ON_COMMAND(ID_VIEW_FLOAT, &CMainFrame::OnFloatPanel)
    ON_COMMAND(ID_VIEW_HIDE, &CMainFrame::OnHidePanel)
END_MESSAGE_MAP()

CMainFrame::CMainFrame() noexcept
{
}

CMainFrame::~CMainFrame()
{
    // 앱 종료 직전: 카메라 설정/패널 상태 저장
    SaveConfigs();

    // 카메라 전부 정리 (스레드 종료/소켓 종료 포함)
    m_CameraManager.DisconnectAll();
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CFrameWnd::PreCreateWindow(cs))
        return FALSE;

    // 스타일이나 크기 조정은 기존 코드 유지
    return TRUE;
}

// ----------------------------------------------------------
// 메인 프레임 생성 직후 초기화
// ----------------------------------------------------------
int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
        return -1;

    // 상태바 만들기
    if (!m_wndStatusBar.Create(this))
        return -1;
    static UINT indicators[] = { ID_SEPARATOR };
    m_wndStatusBar.SetIndicators(indicators, 1);

    // UI 폰트 생성 (버튼에 쓸 깔끔한 폰트)
    LOGFONT lf{};
    lf.lfHeight = -16;
    _tcscpy_s(lf.lfFaceName, _T("Segoe UI"));
    m_FontUI.CreateFontIndirect(&lf);

    // 패널/촬영/설정 버튼 만들기
    // ⚠ 이 문자열(이모지 포함)은 UTF-8 저장 안 되어 있으면 깨질 수 있음
    // 필요하면 "패널"/"촬영"/"설정" 같은 한글로만 바꿔도 됨
    m_btnTogglePanel.Create(_T("패널"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 0, 0), this, ID_VIEW_TOGGLE_LIVEPANEL);
    m_btnManualCapture.Create(_T("촬영"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 0, 0), this, ID_MANUAL_CAPTURE_BTN);
    m_btnSettings.Create(_T("설정"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 0, 0), this, ID_SETTINGS_BTN);

    m_btnTogglePanel.SetFont(&m_FontUI);
    m_btnManualCapture.SetFont(&m_FontUI);
    m_btnSettings.SetFont(&m_FontUI);

    // 카메라 설정 / 패널 상태 불러오기 (config.ini)
    LoadConfigs();              // -> m_CamConfigs, m_LivePanel의 상태/도킹 모드 등 채움

    // --- 기존 ---
    /*
    m_CameraManager.SetNotifyHwnd(m_hWnd);
    m_CameraManager.ApplyConfigs(m_CamConfigs);
    m_CameraManager.ConnectAll();
    */

    for (size_t i = 0; i < m_CamConfigs.size(); ++i)
    {
        const CameraConfig& cfg = m_CamConfigs[i];
        m_CameraManager.ConnectCamera(cfg, m_hWnd);
    }

    // LivePanel 탭 구성 (카메라별 CAM1, CAM2 ...)
    m_LivePanel.BuildTabs(m_CamConfigs);

    // 도킹상태 복원: config.ini에서 읽은 상태(m_LivePanel.m_state)에 맞게
    // Hidden / DockLeft / DockRight / Floating 중 하나로 올린다
    switch (m_LivePanel.m_state)
    {
    case CLivePanel::DockLeft:  m_LivePanel.DockLeftPane();  break;
    case CLivePanel::DockRight: m_LivePanel.DockRightPane(); break;
    case CLivePanel::Floating:  m_LivePanel.FloatPane();     break;
    case CLivePanel::Hidden:
    default:
        // Hidden이면 그냥 숨겨둠. 아직 Create조차 안 됐을 수도 있음.
        break;
    }

    // 뷰 포인터 캐시해두면 편함
    m_pView = DYNAMIC_DOWNCAST(CFactoryVisionClientView, GetActiveView());

    // 창 처음 레이아웃 맞춰주기
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());

    return 0;
}


// ----------------------------------------------------------
// WM_SIZE 들어올 때: 메인 프레임 안에서
// - 상단 버튼 위치
// - LivePanel 위치/폭
// - 중앙 View 위치
// 전부 다시 정렬
// ----------------------------------------------------------
void CMainFrame::OnSize(UINT nType, int cx, int cy)
{
    CFrameWnd::OnSize(nType, cx, cy);

    if (nType == SIZE_MINIMIZED)
        return;

    UpdateLayout(cx, cy);
}


// ----------------------------------------------------------
// 진짜 레이아웃 계산은 여기서
// cx, cy = 현재 클라이언트 영역 크기
// ----------------------------------------------------------
void CMainFrame::UpdateLayout(int cx, int cy)
{
    if (cx <= 0 || cy <= 0)
        return;

    // 1) 상단 버튼들 배치 영역 계산
    //    topBarHeight 만큼 위쪽에 버튼들 가로로 깔고,
    //    그 아래 영역을 본 화면으로 씀
    const int topBarHeight = 40;
    const int btnW = 70;
    const int btnH = 28;
    const int btnMargin = 8;

    int curX = btnMargin;
    int curY = (topBarHeight - btnH) / 2;

    // 패널 토글 버튼
    m_btnTogglePanel.MoveWindow(curX, curY, btnW, btnH);
    curX += btnW + btnMargin;

    // 촬영 버튼
    m_btnManualCapture.MoveWindow(curX, curY, btnW, btnH);
    curX += btnW + btnMargin;

    // 설정 버튼
    m_btnSettings.MoveWindow(curX, curY, btnW, btnH);
    curX += btnW + btnMargin;

    // 카메라 전환 버튼들 (CAM1, CAM2, ...)
    // UpdateCameraButtons()에서 실제 MoveWindow 호출할 거니까 여기서 호출
    CreateDynamicButtonsLayout(); // 버튼 배열(갯수/텍스트) 보장
    UpdateCameraButtons();        // 위치 재배치

    // 2) 남은 클라이언트 영역(= topBarHeight 아래)을 계산
    CRect rcClient(0, 0, cx, cy);
    CRect rcMainArea = rcClient;
    rcMainArea.top += topBarHeight;

    // 3) LivePanel(도킹 상태면 좌/우 차지)을 위한 rect 계산
    CRect rcPanel = rcMainArea;
    CRect rcView = rcMainArea;

    if (IsLivePanelUsable())  // <- 여기 중요. m_LivePanel 안전할 때만 만진다
    {
        if (m_LivePanel.m_state == CLivePanel::DockLeft)
        {
            rcPanel.right = rcPanel.left + m_LivePanel.m_dockWidth;
            rcView.left = rcPanel.right;
        }
        else if (m_LivePanel.m_state == CLivePanel::DockRight)
        {
            rcPanel.left = rcPanel.right - m_LivePanel.m_dockWidth;
            rcView.right = rcPanel.left;
        }

        // 패널이 Hidden이면 그냥 rcPanel은 무시하고 rcView = rcMainArea 유지

        // 도킹 상태면 MoveWindow/ShowWindow
        if (m_LivePanel.m_state == CLivePanel::DockLeft ||
            m_LivePanel.m_state == CLivePanel::DockRight)
        {
            // 패널 위치/크기 맞추기
            m_LivePanel.MoveWindow(rcPanel);

            // 보이게 (또는 숨김)
            m_LivePanel.ShowWindow(SW_SHOW);

            // 탭/프리뷰 재배치
            m_LivePanel.SafeUpdateLayout();
        }
        else
        {
            // Floating or Hidden이면 메인프레임 영역에는 차지 안 시킴
            // Hidden일 때는 굳이 ShowWindow 안 건드림
        }
    }
    else
    {
        // 패널이 아직 생성중이거나(reparent중) 핸들이 없으면
        // 그냥 전체를 View에 준다
        rcView = rcMainArea;
    }

    // 4) 중앙 View(메인 검사 화면) 위치 갱신
    CFactoryVisionClientView* pView = nullptr;
    pView = m_pView ? m_pView
        : DYNAMIC_DOWNCAST(CFactoryVisionClientView, GetActiveView());

    if (pView && ::IsWindow(pView->GetSafeHwnd()))
    {
        pView->MoveWindow(rcView);
    }

    // 5) 상태바 위치는 MFC가 알아서 처리하니까 여기선 안 건드려도 됨
}


// ----------------------------------------------------------
// 카메라별 버튼들 (CAM1, CAM2 ...) 위치/라벨 갱신
// ----------------------------------------------------------
void CMainFrame::CreateDynamicButtonsLayout()
{
    // m_vecCamButtons 에 CAM1, CAM2... 버튼이 없다면 만든다.
    // 이미 있다면 건너뜀.
    // 버튼 텍스트는 "CAM 1", "CAM 2" 이런 식으로.

    // 갯수 맞추기
    int need = (int)m_CamConfigs.size();
    while ((int)m_vecCamButtons.size() < need)
    {
        int idx = (int)m_vecCamButtons.size();
        auto* pBtn = new CButton;
        CString label;
        label.Format(_T("CAM %d"), idx + 1);

        UINT idCmd = ID_CAMERA_BTN_BASE + idx; // ID_CAMERA_BTN_BASE + n

        pBtn->Create(label,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            CRect(0, 0, 0, 0),
            this,
            idCmd);
        pBtn->SetFont(&m_FontUI);
        m_vecCamButtons.push_back(pBtn);
    }

    // 혹시 카메라 수가 줄었으면 나머지는 숨긴다(폭파까지는 안 함)
    for (size_t i = m_CamConfigs.size(); i < m_vecCamButtons.size(); ++i)
    {
        if (m_vecCamButtons[i] && ::IsWindow(m_vecCamButtons[i]->GetSafeHwnd()))
            m_vecCamButtons[i]->ShowWindow(SW_HIDE);
    }
}

void CMainFrame::UpdateCameraButtons()
{
    const int topBarHeight = 40;
    const int btnH = 28;
    const int btnMargin = 8;
    const int baseX = 8 + (70 + btnMargin) * 3; // 패널/촬영/설정 3개 지난 후 x 시작
    int curX = baseX;
    int curY = (topBarHeight - btnH) / 2;

    for (size_t i = 0; i < m_CamConfigs.size(); ++i)
    {
        if (i >= m_vecCamButtons.size()) break;

        CButton* pBtn = m_vecCamButtons[i];
        if (!pBtn || !::IsWindow(pBtn->GetSafeHwnd()))
            continue;

        // 현재 View에서 보고 있는 카메라면 강조 표시 같은 거 하고 싶으면 여기서
        // (예: 텍스트에 "<Active>" 붙이기 등)

        pBtn->MoveWindow(curX, curY, 70, btnH);
        pBtn->ShowWindow(SW_SHOW);

        curX += 70 + btnMargin;
    }
}


// ----------------------------------------------------------
// "CAM n" 버튼 눌렀을 때 → View의 ActiveCamera 바꿔주기
// ----------------------------------------------------------
void CMainFrame::OnCameraViewClick(UINT nID)
{
    int camIdx = (int)(nID - ID_CAMERA_BTN_BASE); // 0-based index

    if (m_pView && ::IsWindow(m_pView->GetSafeHwnd()))
    {
        m_pView->SetActiveCamera(camIdx);
        m_pView->Invalidate(FALSE);
    }
}


// ----------------------------------------------------------
// "촬영" 버튼 눌렀을 때 → 현재 활성 카메라 한 장 강제 전송 트리거
// ----------------------------------------------------------
void CMainFrame::OnManualCaptureClick()
{
    if (!m_pView) return;

    int activeCam = m_pView->GetActiveCameraIndex();
    if (activeCam < 0) return;

    m_CameraManager.TriggerManualCapture(activeCam);
}


// ----------------------------------------------------------
// "설정" 버튼: 카메라 설정 다이얼로그 (IP/포트/모션) 띄우고 저장 후 재연결
// ----------------------------------------------------------
void CMainFrame::OnSettingsClick()
{
    // 너 원본 코드의 CCameraSetupDlg 열고,
    // m_CamConfigs 편집하고,
    // m_CameraManager.DisconnectAll() → ApplyConfigs() → ConnectAll()
    // LivePanel 탭 다시 BuildTabs() 등등 하는 흐름 그대로 유지하되,
    // 마지막에 UpdateLayout() 부를 때도 IsLivePanelUsable() 체크가 걸리게 됐으니 안정적.
}


// ----------------------------------------------------------
// 패널 토글 / 도킹 전환 관련 핸들러들
// ----------------------------------------------------------
void CMainFrame::OnTogglePanel()
{
    // 숨김 <-> 왼쪽 도킹
    m_LivePanel.TogglePane();

    // 도킹상태 바뀌었을 수 있으니까 레이아웃 업데이트
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());
}

void CMainFrame::OnDockLeft()
{
    m_LivePanel.DockLeftPane();
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());
}

void CMainFrame::OnDockRight()
{
    m_LivePanel.DockRightPane();
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());
}

void CMainFrame::OnFloatPanel()
{
    m_LivePanel.FloatPane();
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());
}

void CMainFrame::OnHidePanel()
{
    m_LivePanel.HidePane();
    CRect rc; GetClientRect(&rc);
    UpdateLayout(rc.Width(), rc.Height());
}


// ----------------------------------------------------------
// TCP/카메라 상태 메시지 처리 (상태바 업데이트 등)
// ----------------------------------------------------------
LRESULT CMainFrame::OnTcpStatus(WPARAM wParam, LPARAM lParam)
{
    // 예: wParam = camIndex, lParam=연결/끊김 상태 코드
    // 상태바 텍스트 갱신 등 작성했던 로직 그대로 두면 됨
    return 0;
}

LRESULT CMainFrame::OnCameraDisconnected(WPARAM wParam, LPARAM lParam)
{
    // 특정 카메라 끊김 처리 등
    return 0;
}

LRESULT CMainFrame::OnCameraStatus(WPARAM wParam, LPARAM lParam)
{
    // 카메라 상태 업데이트 등
    return 0;
}


// ----------------------------------------------------------
// 설정 불러오기 / 저장하기 (ini) - 기존 코드 유지
// ----------------------------------------------------------
void CMainFrame::LoadConfigs()
{
    // 기존에 하던 대로 config.ini 읽어서
    // m_CamConfigs 채우고
    // m_LivePanel.ApplyConfig(dockWidth, state, floatRect, fpsCap, jpegQuality);
}

void CMainFrame::SaveConfigs()
{
    // 현재 m_CamConfigs, m_LivePanel 상태(DockState, 위치 등)를
    // config.ini로 써주는 기존 코드 유지
}

CString CMainFrame::GetIniPath() const
{
    TCHAR exePath[MAX_PATH] = { 0 };
    GetModuleFileName(NULL, exePath, MAX_PATH);

    CString folder(exePath);
    int slash = folder.ReverseFind(_T('\\'));
    if (slash >= 0)
        folder = folder.Left(slash);

    // exe 폴더 밑에 config.ini 사용
    CString iniPath = folder + _T("\\config.ini");
    return iniPath;
}


BOOL CMainFrame::LoadCameraConfigs(std::vector<CameraConfig>& out)
{
    out.clear();

    CString ini = GetIniPath();
    TCHAR buf[256]{};

    // 카메라 슬롯은 1부터 MAX_CAMERAS까지 "CAM1", "CAM2", ... 이런 섹션으로 관리
    for (int i = 0; i < MAX_CAMERAS; ++i)
    {
        CString sec;
        sec.Format(_T("CAM%d"), i + 1);

        // 시리얼 가져와서 없으면 이 슬롯은 건너뛴다 (빈 슬롯 취급)
        GetPrivateProfileString(sec, _T("Serial"), _T(""), buf, _countof(buf), ini);
        CString serial = buf;
        if (serial.IsEmpty())
            continue;

        CameraConfig cfg{};
        cfg.nIndex = i;

        cfg.sSerial = serial;

        // FriendlyName: ini에 없으면 "CAM X" 형태로 기본 세팅
        GetPrivateProfileString(sec, _T("Name"), _T(""), buf, _countof(buf), ini);
        if (buf[0] != 0)
            cfg.sFriendlyName = buf;
        else
        {
            cfg.sFriendlyName.Format(_T("CAM %d"), i + 1);
        }

        // IP
        GetPrivateProfileString(sec, _T("IP"), _T("127.0.0.1"), buf, _countof(buf), ini);
        cfg.sIp = buf;

        // Port
        cfg.nPort = GetPrivateProfileInt(sec, _T("Port"), 9000 + i, ini);

        // MotionEnable / Threshold
        cfg.bMotionEnabled = GetPrivateProfileInt(sec, _T("MotionEnable"), 1, ini);
        cfg.nMotionThreshold = GetPrivateProfileInt(sec, _T("Threshold"), 5000, ini);

        out.push_back(cfg);
    }

    // 패널 상태 복원 (도킹/플로팅 정보 등)
    {
        CString secPanel = _T("Panel");

        // DockState
        GetPrivateProfileString(secPanel, _T("DockState"), _T("Hidden"), buf, _countof(buf), ini);
        CLivePanel::DockState st = CLivePanel::Hidden;
        if (_tcscmp(buf, _T("Left")) == 0)       st = CLivePanel::DockLeft;
        else if (_tcscmp(buf, _T("Right")) == 0) st = CLivePanel::DockRight;
        else if (_tcscmp(buf, _T("Floating")) == 0) st = CLivePanel::Floating;
        else if (_tcscmp(buf, _T("Hidden")) == 0)   st = CLivePanel::Hidden;

        // DockWidth
        int dockW = GetPrivateProfileInt(secPanel, _T("DockWidth"), 380, ini);

        // FloatRect "L,T,R,B"
        GetPrivateProfileString(secPanel, _T("FloatRect"),
            _T("100,100,460,620"), buf, _countof(buf), ini);
        int L = 100, T = 100, R = 460, B = 620;
        _stscanf_s(buf, _T("%d,%d,%d,%d"), &L, &T, &R, &B);
        CRect floatRc(L, T, R, B);

        // PreviewFpsCap
        int fpsCap = GetPrivateProfileInt(secPanel, _T("PreviewFpsCap"), 12, ini);

        // JpegQuality
        int jpegQ = GetPrivateProfileInt(secPanel, _T("JpegQuality"), 92, ini);

        // LivePanel 쪽에 적용
        m_LivePanel.ApplyConfig(dockW, st, floatRc, fpsCap, jpegQ);
    }

    return TRUE;
}


BOOL CMainFrame::SaveCameraConfigs(const std::vector<CameraConfig>& cfgs)
{
    CString ini = GetIniPath();

    for (const auto& cfg : cfgs)
    {
        CString sec;
        sec.Format(_T("CAM%d"), cfg.nIndex + 1);

        // Serial
        WritePrivateProfileString(sec, _T("Serial"), cfg.sSerial, ini);

        // Name (우호 이름)
        WritePrivateProfileString(sec, _T("Name"), cfg.sFriendlyName, ini);

        // IP
        WritePrivateProfileString(sec, _T("IP"), cfg.sIp, ini);

        // Port
        {
            CString v;
            v.Format(_T("%d"), cfg.nPort);
            WritePrivateProfileString(sec, _T("Port"), v, ini);
        }

        // MotionEnable
        {
            CString v;
            v.Format(_T("%d"), cfg.bMotionEnabled ? 1 : 0);
            WritePrivateProfileString(sec, _T("MotionEnable"), v, ini);
        }

        // Threshold
        {
            CString v;
            v.Format(_T("%d"), cfg.nMotionThreshold);
            WritePrivateProfileString(sec, _T("Threshold"), v, ini);
        }
    }

    // 패널 상태도 같이 저장해주면 깔끔
    {
        CString secPanel = _T("Panel");

        // DockState 문자열화
        CString dockStr = _T("Hidden");
        switch (m_LivePanel.m_state)
        {
        case CLivePanel::DockLeft:   dockStr = _T("Left");      break;
        case CLivePanel::DockRight:  dockStr = _T("Right");     break;
        case CLivePanel::Floating:   dockStr = _T("Floating");  break;
        case CLivePanel::Hidden:
        default:                     dockStr = _T("Hidden");    break;
        }
        WritePrivateProfileString(secPanel, _T("DockState"), dockStr, ini);

        // DockWidth
        {
            CString v;
            v.Format(_T("%d"), m_LivePanel.m_dockWidth);
            WritePrivateProfileString(secPanel, _T("DockWidth"), v, ini);
        }

        // FloatRect
        {
            CRect r = m_LivePanel.m_floatRect;
            CString v;
            v.Format(_T("%d,%d,%d,%d"), r.left, r.top, r.right, r.bottom);
            WritePrivateProfileString(secPanel, _T("FloatRect"), v, ini);
        }

        // PreviewFpsCap
        {
            CString v;
            v.Format(_T("%d"), m_LivePanel.m_previewFpsCap);
            WritePrivateProfileString(secPanel, _T("PreviewFpsCap"), v, ini);
        }

        // JpegQuality
        {
            CString v;
            v.Format(_T("%d"), m_LivePanel.m_jpegQuality);
            WritePrivateProfileString(secPanel, _T("JpegQuality"), v, ini);
        }
    }

    return TRUE;
}

// ----------------------------------------------------------
// 디버그 유틸 (원래 있던 거 그대로 유지)
// ----------------------------------------------------------
#ifdef _DEBUG
void CMainFrame::AssertValid() const
{
    CFrameWnd::AssertValid();
}
void CMainFrame::Dump(CDumpContext& dc) const
{
    CFrameWnd::Dump(dc);
}
#endif
