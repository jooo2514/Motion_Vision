#pragma once
#include "afxdialogex.h"
#include "afxcmn.h"
#include "SharedData.h"
#include <vector>
#include <opencv2/opencv.hpp>
#include <afxmt.h>

class CPreviewWnd : public CWnd
{
public:
    int CamIndex = -1;
    cv::Mat LastFrame;
    CCriticalSection Lock;
    ULONGLONG LastDrawTick = 0;

    DECLARE_MESSAGE_MAP()
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnPaint();
};

class CFloatLiveFrame; // forward

class CLivePanel : public CDialogEx
{
    DECLARE_DYNAMIC(CLivePanel)

public:
    CLivePanel(CWnd* pParent = nullptr);
    virtual ~CLivePanel();

    enum DockState { Hidden, DockLeft, DockRight, Floating };

    DockState m_state = Hidden;
    int       m_dockWidth = 380;
    CRect     m_floatRect = CRect(100, 100, 460, 620);
    int       m_previewFpsCap = 12;
    int       m_jpegQuality = 92;

    void ApplyConfig(int dockWidth,
        DockState state,
        CRect floatRect,
        int fpsCap,
        int jpegQuality);

    void BuildTabs(const std::vector<CameraConfig>& cfgs);
    void UpdateLayout();
    void SafeUpdateLayout();
    void OnNewFrame(int camIndex, const cv::Mat& frame);

    void DockLeftPane();
    void DockRightPane();
    void FloatPane();
    void HidePane();
    void TogglePane();

    int  GetSelectedCamIndex() const;
    BOOL SaveSnapshotSelected(CString* outPath = nullptr);
    void OpenCaptureFolder();


    bool m_bReparenting = false;


#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_LIVE_PANEL };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX) override;

    // ↓↓↓ 여기 수정됨: override/virtual 제거하고 afx_msg로 선언
    afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);

    CTabCtrl                  m_Tab;
    std::vector<CPreviewWnd*> m_Previews;
    CFloatLiveFrame* m_pFloatFrame = nullptr;

    bool m_resizing = false;
    int  m_resizeGrabX = 0;
    int  m_resizeStartW = 0;
    static const int RESIZE_GRIP = 5;

    void ShowTabContextMenu(CPoint ptScreen);
    void ChangeParent(CWnd* pNewParent);
    void PrepareChangingParent();
    void SafeShow(int nCmdShow);

    DECLARE_MESSAGE_MAP()

public:
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnContextMenu(CWnd* /*pWnd*/, CPoint /*point*/);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
    afx_msg void OnTcnSelChange(NMHDR* pNMHDR, LRESULT* pResult);
    bool IsStableForLayout() const { return !m_bReparenting; }
};
