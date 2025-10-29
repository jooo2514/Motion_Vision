#pragma once

#include <afxres.h>

#define IDR_MAINFRAME               128
#define IDS_APP_TITLE               129
#define IDS_HELLO                   130
#define IDP_OLE_INIT_FAILED         100 // <<< 추가 (표준 MFC ID)

// Icons
#define IDI_MAINFRAME               200
#define IDI_DOCDOC                  201

// Toolbar bitmap
#define IDB_MAINTOOLBAR             300

// Commands (match code)
#define ID_SETTINGS_BTN             (WM_USER + 211)
#define ID_MANUAL_CAPTURE_BTN       (WM_USER + 210)

// Custom camera button base (redeclare to RC scope)
#define ID_CAM_BTN_BASE             (WM_USER + 200)

// Dialogs
#define IDD_ABOUTBOX                100
#define IDD_CAMERA_SETUP            101

// CCameraSetupDlg 컨트롤 ID (Pylon 샘플에서 가져옴)
#define IDC_BUTTON_SEARCH           1001 // <<< 추가
#define IDC_LIST1                   1002 // <<< 추가

#define ID_EXPORT_CSV               140


// ==== Live Panel ====
#define IDD_LIVE_PANEL              3000
#define IDC_TAB_LIVE                3001

// ==== Commands (Buttons / Menu) ====
#define ID_VIEW_TOGGLE_LIVEPANEL    3100   // 도킹 토글 버튼
#define ID_VIEW_DOCK_LEFT           3101
#define ID_VIEW_DOCK_RIGHT          3102
#define ID_VIEW_FLOAT               3103
#define ID_VIEW_HIDE                3104
#define ID_LIVE_SNAPSHOT            3110   // 탭 컨텍스트: 스냅샷 저장
#define ID_LIVE_OPEN_CAPTUREFOLDER  3111   // 탭 컨텍스트: 캡처폴더 열기

// ==== Popup Menu ====
#define IDR_MENU_LIVE_TAB           3200

// 카메라 선택 버튼 명령 ID 시작값.
// 충돌 안 나게 기존 리소스 ID들과 안 겹치는 값 하나 잡으면 된다.
// 만약 Resource.h에 이미 ID_VIEW_..., ID_MANUAL_CAPTURE_BTN 등이 32771 이런 식이면
// 33000 같은 넉넉한 구역을 써주자.

#define ID_CAMERA_BTN_BASE  33000
