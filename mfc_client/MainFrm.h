#pragma once
#include "CCameraManager.h"
#include "CLivePanel.h"
#include "SharedData.h"
#include <vector>

class CMainFrame : public CFrameWnd
{
    DECLARE_DYNCREATE(CMainFrame)
protected:
    CMainFrame() noexcept;

public:
    virtual ~CMainFrame();
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);

#ifdef _DEBUG
    virtual void AssertValid() const;
    virtual void Dump(CDumpContext& dc) const;
#endif

    // === 기존 공개 getter들 ===
    CCameraManager* GetCameraManager() { return &m_CameraManager; }
    CLivePanel* GetLivePanel() { return &m_LivePanel; }

    BOOL LoadCameraConfigs(std::vector<CameraConfig>& out);
    BOOL SaveCameraConfigs(const std::vector<CameraConfig>& cfgs);

protected:
    CStatusBar                 m_wndStatusBar;
    class CFactoryVisionClientView* m_pView = nullptr;
    CCameraManager             m_CameraManager;
    std::vector<CameraConfig>  m_CamConfigs;

    CLivePanel                 m_LivePanel;    // 실시간 패널
    CFont                      m_FontUI;
    CButton                    m_btnTogglePanel, m_btnManualCapture, m_btnSettings;
    std::vector<CButton*>      m_vecCamButtons;

    // === [추가] LivePanel 안전하게 만질 수 있는지 검사하는 헬퍼 ===
    bool IsLivePanelUsable() const
    {
        if (!::IsWindow(m_LivePanel.GetSafeHwnd()))
            return false;
        if (!m_LivePanel.IsStableForLayout()) // 여기만 바뀜
            return false;
        return true;
    }

    // 레이아웃 관련 내부 함수들
    void UpdateLayout(int cx, int cy);
    void CreateDynamicButtonsLayout();
    void UpdateCameraButtons();

    // 설정 저장/로드
    void LoadConfigs();
    void SaveConfigs();
    CString GetIniPath() const;

    DECLARE_MESSAGE_MAP()
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg LRESULT OnTcpStatus(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnCameraDisconnected(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnCameraStatus(WPARAM wParam, LPARAM lParam);
    afx_msg void OnCameraViewClick(UINT nID);
    afx_msg void OnManualCaptureClick();
    afx_msg void OnSettingsClick();

    // 패널 토글/도킹 관련 메시지 핸들러
    afx_msg void OnTogglePanel();
    afx_msg void OnDockLeft();
    afx_msg void OnDockRight();
    afx_msg void OnFloatPanel();
    afx_msg void OnHidePanel();
};
