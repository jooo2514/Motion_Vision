#include "pch.h"
#include "CCameraSetupDlg.h"
#include "resource.h"
#include "CCameraManager.h"
#include "MainFrm.h"
#include <vector>
#include <string>
#include <array>
#include <pylon/DeviceInfo.h>

// [1] CCameraSetupDlg에 대한 런타임 클래스 구현 (CCameraSetupDlg::GetRuntimeClass 해결)
IMPLEMENT_DYNAMIC(CCameraSetupDlg, CDialogEx)

// --- 사용자 확인 필요: Resource.h 에 IDC_BUTTON_SEARCH, IDC_LIST1 정의 확인 ---

BEGIN_MESSAGE_MAP(CCameraSetupDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BUTTON_SEARCH, &CCameraSetupDlg::OnBnClickedSearch) // ID 확인
    ON_BN_CLICKED(IDOK, &CCameraSetupDlg::OnBnClickedSave)
END_MESSAGE_MAP()

CCameraSetupDlg::CCameraSetupDlg(CWnd* p) : CDialogEx(IDD_CAMERA_SETUP, p) {}

void CCameraSetupDlg::DoDataExchange(CDataExchange* pDX) {
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LIST1, m_list); // ID 확인
}

BOOL CCameraSetupDlg::OnInitDialog() {
    CDialogEx::OnInitDialog();

    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);
    m_list.InsertColumn(0, _T("Index"), LVCFMT_LEFT, 60);
    m_list.InsertColumn(1, _T("Serial"), LVCFMT_LEFT, 150);
    m_list.InsertColumn(2, _T("IP"), LVCFMT_LEFT, 120);
    m_list.InsertColumn(3, _T("Port"), LVCFMT_LEFT, 60);
    m_list.InsertColumn(4, _T("Motion"), LVCFMT_LEFT, 60);
    m_list.InsertColumn(5, _T("Threshold"), LVCFMT_LEFT, 80);

    LoadAndDisplay();
    return TRUE;
}

void CCameraSetupDlg::LoadAndDisplay() {
    std::vector<CameraConfig> cfgs;
    LoadFromIni(cfgs);
    m_list.DeleteAllItems();
    for (const auto& c : cfgs) {
        // --- 수정된 부분 (std::to_wstring -> CString::Format) ---
        CString strIndex;
        strIndex.Format(_T("%d"), c.nIndex + 1);
        int row = m_list.InsertItem(m_list.GetItemCount(), strIndex);
        m_list.SetItemText(row, 1, c.sSerial);
        m_list.SetItemText(row, 2, c.sIp);
        CString strPort;
        strPort.Format(_T("%d"), c.nPort);
        m_list.SetItemText(row, 3, strPort);
        m_list.SetItemText(row, 4, c.bMotionEnabled ? _T("ON") : _T("OFF"));
        CString strThreshold;
        strThreshold.Format(_T("%d"), c.nMotionThreshold);
        m_list.SetItemText(row, 5, strThreshold);
        // --- 수정 끝 ---
    }
}

// 검색 버튼
void CCameraSetupDlg::OnBnClickedSearch() {
    auto* mf = dynamic_cast<class CMainFrame*>(AfxGetMainWnd());
    auto* mgr = mf ? mf->GetCameraManager() : nullptr;
    if (!mgr) { AfxMessageBox(_T("Camera manager not ready.")); return; }

    std::vector<Pylon::CDeviceInfo> devs;
    mgr->FindDevices(devs);

    std::vector<CameraConfig> cfgs;
    LoadFromIni(cfgs);
    std::array<bool, MAX_CAMERAS> used{};
    for (auto& c : cfgs)
    {
        if (c.nIndex >= 0 && c.nIndex < MAX_CAMERAS)
            used[c.nIndex] = true;
    }

    for (auto& dv : devs) {
        CString serial(dv.GetSerialNumber());
        bool exists = false;
        for (auto& c : cfgs) {
            if (c.sSerial == serial) { exists = true; break; }
        }
        if (exists) continue;

        int slot = -1;
        for (int i = 0; i < MAX_CAMERAS; ++i) {
            if (!used[i]) { slot = i; break; }
        }
        if (slot != -1) {
            CameraConfig nc{}; nc.nIndex = slot; nc.sSerial = serial;
            nc.sIp = _T("127.0.0.1"); nc.nPort = 5000 + slot;
            nc.bMotionEnabled = FALSE; nc.nMotionThreshold = 1000;
            cfgs.push_back(nc);
            used[slot] = true;
        }
    }

    m_list.DeleteAllItems();
    for (const auto& c : cfgs) {
        // --- 수정된 부분 (std::to_wstring -> CString::Format) ---
        CString strIndex;
        strIndex.Format(_T("%d"), c.nIndex + 1);
        int row = m_list.InsertItem(m_list.GetItemCount(), strIndex);
        m_list.SetItemText(row, 1, c.sSerial);
        m_list.SetItemText(row, 2, c.sIp);
        CString strPort;
        strPort.Format(_T("%d"), c.nPort);
        m_list.SetItemText(row, 3, strPort);
        m_list.SetItemText(row, 4, c.bMotionEnabled ? _T("ON") : _T("OFF"));
        CString strThreshold;
        strThreshold.Format(_T("%d"), c.nMotionThreshold);
        m_list.SetItemText(row, 5, strThreshold);
        // --- 수정 끝 ---
    }
}

// 저장 버튼
void CCameraSetupDlg::OnBnClickedSave() {
    std::vector<CameraConfig> cfgs;
    for (int i = 0; i < m_list.GetItemCount(); ++i) {
        CameraConfig c;
        c.nIndex = _ttoi(m_list.GetItemText(i, 0)) - 1;
        c.sSerial = m_list.GetItemText(i, 1);
        c.sIp = m_list.GetItemText(i, 2);
        c.nPort = _ttoi(m_list.GetItemText(i, 3));
        c.bMotionEnabled = (m_list.GetItemText(i, 4) == _T("ON"));
        c.nMotionThreshold = _ttoi(m_list.GetItemText(i, 5));
        cfgs.push_back(c);
    }
    SaveToIni(cfgs);
    CDialogEx::OnOK();
}

// ini 로딩/저장 (MainFrame 멤버 함수 호출)
BOOL CCameraSetupDlg::LoadFromIni(std::vector<CameraConfig>& out) {
    auto* mf = dynamic_cast<class CMainFrame*>(AfxGetMainWnd());
    return mf ? mf->LoadCameraConfigs(out) : FALSE; // MainFrm.h에 LoadCameraConfigs 선언 확인
}
BOOL CCameraSetupDlg::SaveToIni(const std::vector<CameraConfig>& cfgs) {
    auto* mf = dynamic_cast<class CMainFrame*>(AfxGetMainWnd());
    return mf ? mf->SaveCameraConfigs(cfgs) : FALSE; // MainFrm.h에 SaveCameraConfigs 선언 확인
}