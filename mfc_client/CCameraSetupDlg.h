#pragma once
#include "afxdialogex.h"
#include <vector> // std::vector 사용 위해 추가
#include "SharedData.h" // CameraConfig 사용 위해 추가
#include <afxcmn.h> // CListCtrl 사용 위해 추가

class CCameraSetupDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CCameraSetupDlg)

public:
    CCameraSetupDlg(CWnd* pParent = nullptr);   // 표준 생성자입니다.
    virtual ~CCameraSetupDlg() = default; // 가상 소멸자 추가

    // 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_CAMERA_SETUP }; // IDD_CAMERA_SETUP 확인 필요
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 지원입니다.
    virtual BOOL OnInitDialog(); // OnInitDialog 선언 추가

    // --- 수정된 부분 (멤버 함수 및 변수 선언 추가) ---
    CListCtrl m_list; // 리스트 컨트롤 멤버 변수
    void LoadAndDisplay(); // 함수 선언 추가
    BOOL LoadFromIni(std::vector<CameraConfig>& out); // 함수 선언 추가
    BOOL SaveToIni(const std::vector<CameraConfig>& cfgs); // 함수 선언 추가
    afx_msg void OnBnClickedSearch(); // 버튼 핸들러 선언 추가
    afx_msg void OnBnClickedSave(); // 버튼 핸들러 선언 추가
    // --- 수정 끝 ---

    DECLARE_MESSAGE_MAP()
};