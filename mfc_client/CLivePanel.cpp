#include "pch.h"
#include "CLivePanel.h"
#include "resource.h"
#include "MainFrm.h"
#include "FactoryVisionClientView.h"

#include <Shellapi.h>     // ShellExecute
#include <Shlwapi.h>      // PathAppend, PathRemoveFileSpec
#include <chrono>
#include <string>
#include <algorithm>
#include <vector>
#include <opencv2/imgcodecs.hpp>

#pragma comment(lib, "Shlwapi.lib")

#if _MSVC_LANG >= 201703L || (defined(_MSC_VER) && _MSC_VER >= 1914)
// C++17 이상이면 <filesystem> 사용
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <direct.h>       // _tmkdir
#endif

// ===================================================================
// CPreviewWnd 메시지 맵 / 그리기 관련
// ===================================================================
BEGIN_MESSAGE_MAP(CPreviewWnd, CWnd)
    ON_WM_ERASEBKGND()
    ON_WM_PAINT()
END_MESSAGE_MAP()

BOOL CPreviewWnd::OnEraseBkgnd(CDC* /*pDC*/)
{
    // 기본 배경지우기 안 하고 FALSE -> 깜박임 방지
    return FALSE;
}

void CPreviewWnd::OnPaint()
{
    CPaintDC dc(this); // DC 얻기

    // NOTE:
    // 여기서 LastFrame을 그리면 됨.
    // 지금은 ASSERT 회피 우선 목적이라 빈 처리해도 문제 없음.
    // 실제 렌더링은 추후 필요하면 추가 가능.
    // (이 프로젝트에서 큰 화면 그리기는 CFactoryVisionClientView가 담당)
}


// ===================================================================
// 플로팅용 frame (패널이 떠 있을 때 감싸는 CFrameWnd)
// ===================================================================
class CFloatLiveFrame : public CFrameWnd
{
public:
    CLivePanel* m_pPanel = nullptr; // 안에 실제 패널 다이얼로그

    virtual void PostNcDestroy() override
    {
        // CFrameWnd 기본 파괴 후 delete this
        delete this;
    }

    DECLARE_MESSAGE_MAP()
    afx_msg void OnClose();                       // X 눌렀을 때
    afx_msg void OnMove(int x, int y);            // 이동
    afx_msg void OnSize(UINT nType, int cx, int cy); // 크기 변경
};

BEGIN_MESSAGE_MAP(CFloatLiveFrame, CFrameWnd)
    ON_WM_CLOSE()
    ON_WM_MOVE()
    ON_WM_SIZE()
END_MESSAGE_MAP()

void CFloatLiveFrame::OnClose()
{
    // 사용자가 플로팅 창을 닫으면 실제로 파괴 대신 HidePane() 같은 효과
    if (m_pPanel)
    {
        m_pPanel->ShowWindow(SW_HIDE);                // 패널 숨김
        m_pPanel->m_state = CLivePanel::Hidden;       // 상태 Hidden으로
        if (CWnd* pMain = AfxGetMainWnd())
        {
            pMain->SendMessage(WM_SIZE);              // 메인프레임 레이아웃 갱신
        }
    }
    // CFrameWnd::OnClose() 는 부르면 진짜 창 죽음 -> 우리는 막는다.
    // 기본적으로 닫지 않고 그냥 숨기기만.
}


// 플로팅 상태에서 창을 드래그로 옮길 때 위치를 기억해둔다.
void CFloatLiveFrame::OnMove(int x, int y)
{
    CFrameWnd::OnMove(x, y);

    if (m_pPanel && ::IsWindow(m_pPanel->GetSafeHwnd()))
    {
        WINDOWPLACEMENT wp{};
        GetWindowPlacement(&wp);

        // 최소/최대화 아닐 때만 정상 위치 저장
        if (wp.showCmd != SW_SHOWMINIMIZED && wp.showCmd != SW_SHOWMAXIMIZED)
            m_pPanel->m_floatRect = wp.rcNormalPosition;
    }
}

void CFloatLiveFrame::OnSize(UINT nType, int cx, int cy)
{
    CFrameWnd::OnSize(nType, cx, cy);

    if (m_pPanel && ::IsWindow(m_pPanel->GetSafeHwnd()))
    {
        // 플로팅 프레임 크기 바뀔 때 내부 패널도 맞춰준다.
        m_pPanel->MoveWindow(0, 0, cx, cy);

        // 여기서 바로 UpdateLayout() 부르면 reparent 중간일 때 ASSERT 날 수 있으므로
        // 안전 래퍼 사용
        m_pPanel->SafeUpdateLayout();

        // 최소/최대화 아닐 땐 floatRect 갱신
        if (nType != SIZE_MINIMIZED && nType != SIZE_MAXIMIZED)
        {
            WINDOWPLACEMENT wp{};
            GetWindowPlacement(&wp);
            m_pPanel->m_floatRect = wp.rcNormalPosition;
        }
    }
}


// ===================================================================
// CLivePanel 본체
// ===================================================================

IMPLEMENT_DYNAMIC(CLivePanel, CDialogEx)

BEGIN_MESSAGE_MAP(CLivePanel, CDialogEx)
    ON_WM_SIZE()
    ON_WM_CONTEXTMENU()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_SETCURSOR()
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_NOTIFY(TCN_SELCHANGE, IDC_TAB_LIVE, &CLivePanel::OnTcnSelChange)
END_MESSAGE_MAP()

CLivePanel::CLivePanel(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_LIVE_PANEL, pParent)
    , m_pFloatFrame(nullptr)
    , m_state(Hidden)
    , m_dockWidth(380)
    , m_resizing(false)
    , m_resizeGrabX(0)
    , m_resizeStartW(0)
    , m_previewFpsCap(12)
    , m_jpegQuality(92)
    , m_bReparenting(false) // 초기에는 reparent 아님
{
    m_floatRect = CRect(100, 100, 460, 620); // 기본 플로팅 위치/사이즈
}

CLivePanel::~CLivePanel()
{
    // 탭 프리뷰 윈도우들 정리
    for (auto* p : m_Previews)
        delete p;
    m_Previews.clear();

    // 플로팅 프레임 살아있으면 정리
    if (m_pFloatFrame && m_pFloatFrame->GetSafeHwnd())
    {
        m_pFloatFrame->DestroyWindow();
    }
    m_pFloatFrame = nullptr;
}

int CLivePanel::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CDialogEx::OnCreate(lpCreateStruct) == -1)
        return -1;

    // 탭 컨트롤 생성
    if (!m_Tab.Create(
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | TCS_TOOLTIPS,
        CRect(0, 0, 10, 10),
        this,
        IDC_TAB_LIVE))
    {
        TRACE0("Failed to create tab control\n");
        return -1;
    }

    return 0;
}

void CLivePanel::OnDestroy()
{
    // 플로팅 상태였다면 마지막 위치 저장
    if (m_state == Floating && m_pFloatFrame && m_pFloatFrame->GetSafeHwnd())
    {
        WINDOWPLACEMENT wp{};
        m_pFloatFrame->GetWindowPlacement(&wp);
        if (wp.showCmd != SW_SHOWMINIMIZED && wp.showCmd != SW_SHOWMAXIMIZED)
            m_floatRect = wp.rcNormalPosition;
    }

    CDialogEx::OnDestroy();
}

void CLivePanel::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);

    // m_Tab과 대화상자 리소스(IDC_TAB_LIVE)를 연결
    DDX_Control(pDX, IDC_TAB_LIVE, m_Tab);
}

BOOL CLivePanel::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
    LPNMHDR p = (LPNMHDR)lParam;
    if (p != nullptr && p->hwndFrom == m_Tab.m_hWnd)
    {
        if (p->code == TCN_SELCHANGE)
        {
            OnTcnSelChange(p, pResult);
        }
        else if (p->code == NM_RCLICK)
        {
            CPoint pt; GetCursorPos(&pt);
            ShowTabContextMenu(pt);
        }
    }
    return CDialogEx::OnNotify(wParam, lParam, pResult);
}


// ------------------------------------------------------------
// 설정값 적용 (ini에서 읽은 것 반영)
// ------------------------------------------------------------
void CLivePanel::ApplyConfig(int dockWidth,
    DockState state,
    CRect floatRect,
    int fpsCap,
    int jpegQuality)
{
    m_dockWidth = dockWidth;
    m_state = state;
    m_floatRect = floatRect;
    m_previewFpsCap = fpsCap;
    m_jpegQuality = jpegQuality;
}


// ------------------------------------------------------------
// 탭 구성: 카메라 리스트에 맞춰 CAM1, CAM2 ... 탭 만들고
// 각 탭마다 CPreviewWnd 생성해서 붙임
// ------------------------------------------------------------
void CLivePanel::BuildTabs(const std::vector<CameraConfig>& cfgs)
{
    if (!m_Tab.GetSafeHwnd())
        return;

    // 기존 프리뷰들 정리
    for (auto* p : m_Previews)
    {
        if (p && p->GetSafeHwnd())
            p->DestroyWindow();
        delete p;
    }
    m_Previews.clear();
    m_Tab.DeleteAllItems();

    // 새로 추가
    for (size_t i = 0; i < cfgs.size(); ++i)
    {
        CString t; t.Format(_T("CAM %d"), cfgs[i].nIndex + 1); // 탭 이름
        TCITEM ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = t.GetBuffer();
        m_Tab.InsertItem((int)i, &ti);
        t.ReleaseBuffer();

        // 각 탭마다 프리뷰용 child window 만들기
        auto* pv = new CPreviewWnd();
        pv->CamIndex = cfgs[i].nIndex;

        CRect rc(0, 0, 10, 10);
        BOOL ok = pv->Create(
            AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW),
            _T("Preview"),
            WS_CHILD | WS_CLIPSIBLINGS,
            rc,
            &m_Tab,
            0);

        if (!ok)
        {
            TRACE1("Failed to create preview window for CAM %d\n", cfgs[i].nIndex + 1);
            delete pv;
            continue;
        }
        m_Previews.push_back(pv);
    }

    // 탭/레이아웃 정리
    SafeUpdateLayout();

    // 첫 탭을 선택해 표시
    if (m_Tab.GetItemCount() > 0)
    {
        m_Tab.SetCurSel(0);
        OnTcnSelChange(nullptr, nullptr);
    }
}


// ------------------------------------------------------------
// 레이아웃 재계산 (탭 전체 크기 / 각 프리뷰 창 위치/보이기)
// 원본 UpdateLayout은 그대로 두고, SafeUpdateLayout에서 reparent 중엔 skip
// ------------------------------------------------------------
void CLivePanel::UpdateLayout()
{
    if (!GetSafeHwnd() || !m_Tab.GetSafeHwnd())
        return;

    // 도킹인지 플로팅인지에 따라 클라이언트 영역 가져오기
    CRect rcClient;
    if (m_state == Floating && m_pFloatFrame && m_pFloatFrame->GetSafeHwnd())
        m_pFloatFrame->GetClientRect(&rcClient);
    else
        GetClientRect(&rcClient);

    // 탭 ctrl 배치
    CRect rcTab = rcClient;
    rcTab.DeflateRect(6, 6);
    if (::IsWindow(m_Tab.GetSafeHwnd()))
        m_Tab.MoveWindow(&rcTab);

    // 탭 내부 영역 계산
    CRect rcPane;
    if (::IsWindow(m_Tab.GetSafeHwnd()))
    {
        m_Tab.GetClientRect(&rcPane);
        m_Tab.AdjustRect(FALSE, &rcPane);

        int currentTab = m_Tab.GetCurSel();
        for (size_t i = 0; i < m_Previews.size(); ++i)
        {
            if (m_Previews[i] && m_Previews[i]->GetSafeHwnd())
            {
                // 프리뷰 창을 탭 영역에 맞춰 크기 조정
                m_Previews[i]->MoveWindow(&rcPane);

                // 현재 선택된 탭만 보이게
                m_Previews[i]->ShowWindow((int)i == currentTab ? SW_SHOW : SW_HIDE);
            }
        }
    }
}

// 안전 래퍼: reparent 중이면 아무 것도 안 함
void CLivePanel::SafeUpdateLayout()
{
    if (m_bReparenting)
        return; // 부모 갈아끼우는 중에는 건드리지 않음
    UpdateLayout();
}


// ------------------------------------------------------------
// 새로운 프레임 도착 (카메라 스레드 -> View -> 여기 호출)
// 각 PreviewWnd에 이미지 복사하고 Invalidate
// FPS 제한(m_previewFpsCap)도 여기서 관리
// ------------------------------------------------------------
void CLivePanel::OnNewFrame(int camIndex, const cv::Mat& frame)
{
    if (frame.empty())
        return;

    const ULONGLONG now = GetTickCount64();
    const ULONGLONG minGap = (m_previewFpsCap > 0)
        ? (1000ULL / (ULONGLONG)m_previewFpsCap)
        : 0ULL;

    for (auto* pv : m_Previews)
    {
        if (!pv) continue;
        if (pv->CamIndex != camIndex) continue;
        if (!pv->GetSafeHwnd()) continue; // 아직/이미 창이 없으면 스킵

        // 프레임 복사 (락 사용)
        {
            CSingleLock lk(&pv->Lock, TRUE);
            pv->LastFrame = frame.clone();
        }

        // FPS 제한 걸어서 너무 자주 Invalidate 안 함
        if (minGap == 0 || now - pv->LastDrawTick >= minGap)
        {
            pv->LastDrawTick = now;

            // reparent 중엔 Invalidate도 하지 말자 -> ASSERT 방어
            if (!m_bReparenting)
                pv->Invalidate(FALSE);
        }
    }
}


// ------------------------------------------------------------
// 도킹 / 플로팅 / 숨김 상태 전환
// ChangeParent() / PrepareChangingParent() / SafeShow() 안에서
// m_bReparenting을 true로 켰다가 마지막에 false로 꺼준다.
// ------------------------------------------------------------

// 부모 교체하기 (WS_CHILD <-> WS_POPUP 등 스타일까지 조정)
void CLivePanel::ChangeParent(CWnd* pNewParent)
{
    if (!GetSafeHwnd())
        return;

    CWnd* pCurrentParent = GetParent();
    if (pNewParent == pCurrentParent)
        return;

    // 여기서부터 reparent 보호 구간
    m_bReparenting = true;

    PrepareChangingParent(); // 자식들을 잠깐 parent=nullptr로 떼어낸다.

    if (pNewParent)
    {
        // 자식 다이얼로그 스타일
        ModifyStyle(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU,
            WS_CHILD);
        ModifyStyleEx(WS_EX_TOOLWINDOW | WS_EX_APPWINDOW, 0);

        SetParent(pNewParent);

        // 프레임 스타일 갱신
        SetWindowPos(
            nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE |
            SWP_NOZORDER | SWP_FRAMECHANGED);

        SafeShow(SW_SHOW); // 안전하게 ShowWindow
    }
    else
    {
        // 플로팅으로 나갈 때는 WS_POPUP 스타일
        ModifyStyle(WS_CHILD, WS_POPUP);
        ModifyStyleEx(0, WS_EX_TOOLWINDOW);
        SetParent(nullptr);
    }

    m_bReparenting = false;
}

// 부모 바꾸기 직전에 호출: 탭/프리뷰들을 일단 잠깐 부모에서 떼어냄
void CLivePanel::PrepareChangingParent()
{
    // 중간 상태에서 레이아웃/Invalidate 등 부르면 ASSERT 나니까
    // m_bReparenting 은 ChangeParent/FloatPane/DockPane 쪽에서 세팅해줌.

    if (m_Tab.GetSafeHwnd())
        m_Tab.SetParent(nullptr);

    for (auto* pv : m_Previews)
    {
        if (pv && pv->GetSafeHwnd())
            pv->SetParent(nullptr);
    }
}

// reparent 또는 상태 전환 도중엔 무리하게 ShowWindow/HideWindow 하지 않도록 래핑
void CLivePanel::SafeShow(int nCmdShow)
{
    if (!GetSafeHwnd())
        return;
    if (m_bReparenting)
    {
        // 지금은 parent 갈아끼우는 중이므로 강제 Show/Hide는 스킵
        return;
    }
    ShowWindow(nCmdShow);
}


// 왼쪽 도킹
void CLivePanel::DockLeftPane()
{
    CWnd* pMainFrame = AfxGetMainWnd();
    if (!pMainFrame)
        return;

    // 만약 지금 Floating 상태라면 플로팅 프레임 파괴
    if (m_state == Floating && m_pFloatFrame && m_pFloatFrame->GetSafeHwnd())
    {
        m_bReparenting = true;

        PrepareChangingParent();
        m_pFloatFrame->DestroyWindow();
        m_pFloatFrame = nullptr;

        m_bReparenting = false;
    }

    // 아직 안 만들어졌으면 Create
    if (!GetSafeHwnd())
    {
        if (!Create(IDD_LIVE_PANEL, pMainFrame))
            return;
    }
    else
    {
        // 이미 만들어진 상태면 부모만 바꿔서 Child로 붙이기
        ChangeParent(pMainFrame);
    }

    m_state = DockLeft;

    // 메인프레임한테 레이아웃 갱신 요청
    if (pMainFrame)
        pMainFrame->SendMessage(WM_SIZE);

    SafeShow(SW_SHOW);
    SafeUpdateLayout();
}

// 오른쪽 도킹
void CLivePanel::DockRightPane()
{
    CWnd* pMainFrame = AfxGetMainWnd();
    if (!pMainFrame)
        return;

    if (m_state == Floating && m_pFloatFrame && m_pFloatFrame->GetSafeHwnd())
    {
        m_bReparenting = true;

        PrepareChangingParent();
        m_pFloatFrame->DestroyWindow();
        m_pFloatFrame = nullptr;

        m_bReparenting = false;
    }

    if (!GetSafeHwnd())
    {
        if (!Create(IDD_LIVE_PANEL, pMainFrame))
            return;
    }
    else
    {
        ChangeParent(pMainFrame);
    }

    m_state = DockRight;

    if (pMainFrame)
        pMainFrame->SendMessage(WM_SIZE);

    SafeShow(SW_SHOW);
    SafeUpdateLayout();
}

// 플로팅 (별도의 CFloatLiveFrame 안으로 분리)
void CLivePanel::FloatPane()
{
    // 이미 떠있는 상태면 맨 앞으로만
    if (m_state == Floating)
    {
        if (m_pFloatFrame && m_pFloatFrame->GetSafeHwnd())
            m_pFloatFrame->BringWindowToTop();
        return;
    }

    // 새 플로팅 프레임 생성
    m_pFloatFrame = new CFloatLiveFrame();
    m_pFloatFrame->m_pPanel = this;

    if (!m_pFloatFrame->Create(
        nullptr,
        _T("Live Panel"),
        WS_OVERLAPPEDWINDOW,
        m_floatRect,
        nullptr))
    {
        delete m_pFloatFrame;
        m_pFloatFrame = nullptr;
        AfxMessageBox(_T("Failed to create floating window"));
        return;
    }

    // 이제 이 패널을 그 플로팅 프레임의 child처럼 붙인다.
    m_bReparenting = true;

    if (GetSafeHwnd())
    {
        ChangeParent(m_pFloatFrame);

        // 플로팅 프레임 크기에 맞춰서 이 다이얼로그도 리사이즈
        SetWindowPos(nullptr,
            0, 0,
            m_floatRect.Width(),
            m_floatRect.Height(),
            SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    else
    {
        // 아직 패널이 안 만들어졌다면 지금 만들어서 붙임
        if (!Create(IDD_LIVE_PANEL, m_pFloatFrame))
        {
            m_pFloatFrame->DestroyWindow();
            m_pFloatFrame = nullptr;
            AfxMessageBox(_T("Failed to create panel"));
            m_bReparenting = false;
            return;
        }

        SetWindowPos(nullptr,
            0, 0,
            m_floatRect.Width(),
            m_floatRect.Height(),
            SWP_NOZORDER | SWP_FRAMECHANGED);
    }

    m_bReparenting = false;

    // 플로팅 프레임 보여주고 갱신
    m_pFloatFrame->ShowWindow(SW_SHOW);
    m_pFloatFrame->UpdateWindow();

    m_state = Floating;

    // 메인프레임에도 사이즈 갱신 요청 (도킹 레이아웃이 바뀌었으니까)
    if (CWnd* pMain = AfxGetMainWnd())
        pMain->SendMessage(WM_SIZE);

    SafeUpdateLayout();
}

// 숨김 (Hidden)
void CLivePanel::HidePane()
{
    // 플로팅 상태면 플로팅 프레임만 숨기고 상태 Hidden
    if (m_state == Floating && m_pFloatFrame && m_pFloatFrame->GetSafeHwnd())
    {
        SafeShow(SW_HIDE); // 패널 자체를 숨김
        m_pFloatFrame->ShowWindow(SW_HIDE);
    }
    else if (GetSafeHwnd())
    {
        SafeShow(SW_HIDE); // 도킹된 상태라면 그냥 숨김
    }

    m_state = Hidden;

    // 부모 프레임에게 레이아웃 갱신 요청
    CWnd* pParentWnd = GetParent();
    if (pParentWnd)
        pParentWnd->SendMessage(WM_SIZE);
}

// 숨김 <-> 왼쪽 도킹 토글 (기본 동작)
void CLivePanel::TogglePane()
{
    if (m_state == Hidden)
        DockLeftPane();
    else
        HidePane();
}


// ------------------------------------------------------------
// 탭 선택 바뀌면, 현재 탭만 ShowWindow(SW_SHOW)
// 그리고 메인 View 쪽에 "활성 카메라 바뀜" 알려줌
// ------------------------------------------------------------
void CLivePanel::OnTcnSelChange(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
    int sel = m_Tab.GetCurSel();
    if (sel >= 0 && sel < (int)m_Previews.size())
    {
        // 선택된 탭만 보이도록
        for (size_t i = 0; i < m_Previews.size(); ++i)
        {
            if (m_Previews[i] && m_Previews[i]->GetSafeHwnd())
            {
                m_Previews[i]->ShowWindow((int)i == sel ? SW_SHOW : SW_HIDE);
            }
        }

        // 선택된 탭은 Invalidate 해서 바로 갱신
        if (m_Previews[sel] && m_Previews[sel]->GetSafeHwnd())
        {
            if (!m_bReparenting)
                m_Previews[sel]->Invalidate(FALSE);
        }

        // 메인 뷰(CFactoryVisionClientView)에도 "지금 활성 카메라 바뀜" 알려주기
        int cam = m_Previews[sel]->CamIndex;
        if (CWnd* pMain = AfxGetMainWnd())
        {
            auto* pFrm = dynamic_cast<CMainFrame*>(pMain);
            if (pFrm && pFrm->GetSafeHwnd() && pFrm->GetActiveView())
            {
                auto* v = dynamic_cast<CFactoryVisionClientView*>(pFrm->GetActiveView());
                if (v)
                    v->SetActiveCamera(cam);
            }
        }
    }

    if (pResult)
        *pResult = 0;
}


// ------------------------------------------------------------
// 우클릭 메뉴
// ------------------------------------------------------------
void CLivePanel::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
    ShowTabContextMenu(point);
}

void CLivePanel::ShowTabContextMenu(CPoint ptScreen)
{
    CMenu m;
    m.LoadMenu(IDR_MENU_LIVE_TAB);
    CMenu* popup = m.GetSubMenu(0);
    if (!popup)
        return;

    // 체크/활성 상태 반영
    popup->CheckMenuItem(ID_VIEW_DOCK_LEFT,
        MF_BYCOMMAND | (m_state == DockLeft ? MF_CHECKED : MF_UNCHECKED));
    popup->CheckMenuItem(ID_VIEW_DOCK_RIGHT,
        MF_BYCOMMAND | (m_state == DockRight ? MF_CHECKED : MF_UNCHECKED));
    popup->CheckMenuItem(ID_VIEW_FLOAT,
        MF_BYCOMMAND | (m_state == Floating ? MF_CHECKED : MF_UNCHECKED));
    popup->EnableMenuItem(ID_VIEW_HIDE,
        MF_BYCOMMAND | (m_state != Hidden ? MF_ENABLED : MF_GRAYED));

    UINT id = (UINT)popup->TrackPopupMenu(
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        ptScreen.x,
        ptScreen.y,
        this);

    switch (id)
    {
    case ID_LIVE_SNAPSHOT:           SaveSnapshotSelected();    break;
    case ID_LIVE_OPEN_CAPTUREFOLDER: OpenCaptureFolder();       break;
    case ID_VIEW_DOCK_LEFT:          DockLeftPane();            break;
    case ID_VIEW_DOCK_RIGHT:         DockRightPane();           break;
    case ID_VIEW_FLOAT:              FloatPane();               break;
    case ID_VIEW_HIDE:               HidePane();                break;
    default: break;
    }
}


// ------------------------------------------------------------
// 현재 탭이 가리키는 카메라 index 가져오기
// ------------------------------------------------------------
int CLivePanel::GetSelectedCamIndex() const
{
    int sel = m_Tab.GetCurSel();
    if (sel < 0 || sel >= (int)m_Previews.size() || !m_Previews[sel])
        return -1;
    return m_Previews[sel]->CamIndex;
}


// ------------------------------------------------------------
// 현재 탭 이미지를 캡처 폴더에 JPG로 저장
// ------------------------------------------------------------
BOOL CLivePanel::SaveSnapshotSelected(CString* outPath)
{
    int cam = GetSelectedCamIndex();
    if (cam < 0)
        return FALSE;

    CPreviewWnd* pv = nullptr;
    for (auto* p : m_Previews)
    {
        if (p && p->CamIndex == cam)
        {
            pv = p;
            break;
        }
    }
    if (!pv)
        return FALSE;

    // 프레임 가져오기
    cv::Mat img;
    {
        CSingleLock lk(&pv->Lock, TRUE);
        if (!pv->LastFrame.empty())
            img = pv->LastFrame.clone();
    }

    if (img.empty())
        return FALSE;

    // 실행폴더\captures 폴더 경로 구성
    TCHAR exe[MAX_PATH]{};
    GetModuleFileName(NULL, exe, MAX_PATH);
    PathRemoveFileSpec(exe);
    PathAppend(exe, _T("captures"));
    CString capDir = exe;

#if _MSVC_LANG >= 201703L || (defined(_MSC_VER) && _MSC_VER >= 1914)
    if (!fs::exists((LPCTSTR)capDir))
    {
        fs::create_directories((LPCTSTR)capDir);
    }
#else
    if (GetFileAttributes(capDir) == INVALID_FILE_ATTRIBUTES)
    {
        _tmkdir(capDir);
    }
#endif

    // 파일명 CAMX_YYYYMMDD_HHMMSS.jpg
    SYSTEMTIME st; GetLocalTime(&st);
    CString name;
    name.Format(
        _T("CAM%d_%04d%02d%02d_%02d%02d%02d.jpg"),
        cam + 1,
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    CString full = capDir + _T("\\") + name;

    // JPEG 품질 설정
    std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, m_jpegQuality };

    // CString -> std::string
    std::string path_std = static_cast<std::string>(ATL::CW2A(full));

    bool ok = cv::imwrite(path_std, img, params);
    if (ok)
    {
        if (outPath)
            *outPath = full;
        MessageBeep(MB_ICONINFORMATION);
        return TRUE;
    }

    MessageBeep(MB_ICONHAND);
    return FALSE;
}


// ------------------------------------------------------------
// 캡처 폴더 열기
// ------------------------------------------------------------
void CLivePanel::OpenCaptureFolder()
{
    TCHAR exe[MAX_PATH]{};
    GetModuleFileName(NULL, exe, MAX_PATH);
    PathRemoveFileSpec(exe);
    PathAppend(exe, _T("captures"));
    ShellExecute(NULL, _T("open"), exe, NULL, NULL, SW_SHOWNORMAL);
}


// ------------------------------------------------------------
// 윈도우 리사이즈 시그널 (도킹 상태에서 주로 호출)
// reparent 중이면 그냥 무시한다.
// ------------------------------------------------------------
void CLivePanel::OnSize(UINT nType, int cx, int cy)
{
    if (!GetSafeHwnd())
        return;

    CDialogEx::OnSize(nType, cx, cy);

    if (m_bReparenting)
        return; // 부모 갈아끼우는 중이면 레이아웃 건들지 않음

    SafeUpdateLayout();
}


// ------------------------------------------------------------
// 폭 리사이즈 시작(LButtonDown): 도킹 상태에서 패널 폭을 마우스로 드래그
// reparent 중이면 아무 것도 안 함
// ------------------------------------------------------------
void CLivePanel::OnLButtonDown(UINT nFlags, CPoint pt)
{
    if (!GetSafeHwnd())
        return;

    if (!m_bReparenting)
    {
        if ((m_state == DockLeft || m_state == DockRight) && !m_resizing)
        {
            CRect rc; GetClientRect(&rc);
            bool onEdge = false;
            if (m_state == DockLeft)  onEdge = (rc.right - pt.x) <= RESIZE_GRIP;
            if (m_state == DockRight) onEdge = (pt.x - rc.left) <= RESIZE_GRIP;

            if (onEdge)
            {
                m_resizing = true;
                m_resizeGrabX = pt.x;
                m_resizeStartW = m_dockWidth;
                SetCapture(); // 마우스 캡처해서 드래그 추적
            }
        }
    }

    if (m_state != Floating)
    {
        // 도킹 상태일 땐 기본 CDialogEx 처리 호출
        CDialogEx::OnLButtonDown(nFlags, pt);
    }
    // 플로팅 상태일 땐 CFloatLiveFrame이 외곽 프레임이라 여기서 따로 안 건드려도 됨
}


// ------------------------------------------------------------
// 마우스 버튼 놓았을 때: 폭조절 종료
// reparent 중이면 그냥 스킵
// ------------------------------------------------------------
void CLivePanel::OnLButtonUp(UINT nFlags, CPoint pt)
{
    if (!GetSafeHwnd())
        return;

    if (m_resizing)
    {
        m_resizing = false;
        ReleaseCapture();

        // 폭이 바뀌었으면 메인프레임 레이아웃 갱신
        if (CWnd* pMain = AfxGetMainWnd())
            pMain->SendMessage(WM_SIZE);
    }

    if (m_state != Floating)
    {
        CDialogEx::OnLButtonUp(nFlags, pt);
    }
}


// ------------------------------------------------------------
// 마우스 이동 중: 폭조절 중이면 도킹 패널 폭 갱신
// reparent 중이면 스킵
// ------------------------------------------------------------
void CLivePanel::OnMouseMove(UINT nFlags, CPoint pt)
{
    if (!GetSafeHwnd())
        return;

    if (!m_bReparenting)
    {
        if (m_resizing && (nFlags & MK_LBUTTON))
        {
            int delta = pt.x - m_resizeGrabX;
            if (m_state == DockLeft)
                m_dockWidth = std::max(160, m_resizeStartW + delta);
            if (m_state == DockRight)
                m_dockWidth = std::max(160, m_resizeStartW - delta);

            if (CWnd* pMain = AfxGetMainWnd())
                pMain->SendMessage(WM_SIZE); // 메인프레임이 패널/뷰 레이아웃 다시 계산
        }
    }

    if (m_state != Floating)
    {
        CDialogEx::OnMouseMove(nFlags, pt);
    }
}


// ------------------------------------------------------------
// 마우스 커서 모양 변경: 도킹 상태에서 가장자리면 좌우 리사이즈 커서
// reparent 도중에는 그냥 기본 커서로 넘어감
// ------------------------------------------------------------
BOOL CLivePanel::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
    // 아직/이미 HWND 없는 상태면 깊게 안 들어가고 기본 처리로 넘김
    if (!GetSafeHwnd())
        return FALSE;

    if (m_bReparenting)
    {
        // 부모 바꾸는 중이면 커서 처리도 건들지 않는다.
        return FALSE;
    }

    if ((m_state == DockLeft || m_state == DockRight) && !m_resizing)
    {
        CPoint pt; GetCursorPos(&pt);
        ScreenToClient(&pt);

        CRect rc; GetClientRect(&rc);

        bool onEdge = false;
        if (m_state == DockLeft)
            onEdge = (rc.right - pt.x) <= RESIZE_GRIP;
        if (m_state == DockRight)
            onEdge = (pt.x - rc.left) <= RESIZE_GRIP;

        if (onEdge)
        {
            ::SetCursor(AfxGetApp()->LoadStandardCursor(IDC_SIZEWE));
            return TRUE; // 커서 우리가 처리했다
        }
    }

    // 도킹 상태면 CDialogEx의 OnSetCursor로 넘기고,
    // 플로팅 상태면 그냥 CWnd::OnSetCursor 호출하는 기존 로직 유지
    if (m_state != Floating)
        return CDialogEx::OnSetCursor(pWnd, nHitTest, message);
    else
        return CWnd::OnSetCursor(pWnd, nHitTest, message);
}
