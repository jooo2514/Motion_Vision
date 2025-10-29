#include "pch.h"
#include "CCameraManager.h"
#include "MainFrm.h"
#include <string>
// #include <Pylon_TLParams.h> // 찾을 수 없음 - Pylon SDK 포함 경로 확인 필요
#include <pylon/PylonIncludes.h> // Pylon 기본 헤더 포함

using namespace Pylon;
using namespace GenApi; // String_t 사용 위해 추가 (선택 사항)

CCameraManager::CCameraManager() {
    memset(m_hGrabThreads, 0, sizeof(m_hGrabThreads));
    memset(m_bThreadStopFlags, 0, sizeof(m_bThreadStopFlags));
    memset(m_bManualTrigger, 0, sizeof(m_bManualTrigger));
}
CCameraManager::~CCameraManager() { DisconnectAll(); }

void CCameraManager::FindDevices(std::vector<Pylon::CDeviceInfo>& out) {
    try {
        CTlFactory& f = CTlFactory::GetInstance();
        DeviceInfoList_t list;
        f.EnumerateDevices(list);
        out.clear();
        for (auto& d : list) out.push_back(d);
    }
    catch (const GenericException& e) {
        AfxMessageBox(CString(e.GetDescription()), MB_ICONERROR);
        out.clear();
    }
}

BOOL CCameraManager::ConnectCamera(const CameraConfig& cfg, HWND hNotifyWnd) {
    const int i = cfg.nIndex;
    if (i < 0 || i >= MAX_CAMERAS || m_Cameras[i].IsPylonDeviceAttached()) return FALSE;

    try {
        CDeviceInfo info;
        // --- 수정된 부분 (Pylon::String_t으로 변환) ---
        // Pylon::String_t은 보통 std::string 또는 Pylon 내부 문자열 타입입니다.
        // CT2A는 const char*를 반환하므로 이를 String_t으로 변환합니다.
        info.SetSerialNumber(Pylon::String_t(CT2A(cfg.sSerial)));
        // --- 수정 끝 ---
        m_Cameras[i].Attach(CTlFactory::GetInstance().CreateDevice(info));
        m_Cameras[i].Open();

        if (!m_Comms[i].Connect(cfg.sIp, cfg.nPort, hNotifyWnd, i)) {
            if (m_Cameras[i].IsOpen()) m_Cameras[i].Close();
            if (m_Cameras[i].IsPylonDeviceAttached()) m_Cameras[i].DetachDevice();
            return FALSE;
        }

        m_Detectors[i] = cv::createBackgroundSubtractorMOG2();
        UpdateMotionSettings(i, cfg.bMotionEnabled, cfg.nMotionThreshold);
        m_bManualTrigger[i] = FALSE;

        m_bThreadStopFlags[i] = FALSE;
        m_ThreadParams[i] = {};
        m_ThreadParams[i].nCameraIndex = i;
        m_ThreadParams[i].hNotifyWnd = hNotifyWnd;
        m_ThreadParams[i].pCamera = &m_Cameras[i];
        m_ThreadParams[i].pCommLock = m_Comms[i].GetLock();
        m_ThreadParams[i].pCommunicator = &m_Comms[i];
        m_ThreadParams[i].pDetector = (void*)m_Detectors[i].get();
        m_ThreadParams[i].config = cfg;

        unsigned tid;
        m_hGrabThreads[i] = (HANDLE)_beginthreadex(nullptr, 0, GrabThread, &m_ThreadParams[i], 0, &tid);
        if (!m_hGrabThreads[i]) { DisconnectCamera(i); return FALSE; }

        m_Cameras[i].StartGrabbing(GrabStrategy_LatestImageOnly);
    }
    catch (const GenericException& e) {
        DisconnectCamera(i);
        AfxMessageBox(CString(e.GetDescription()), MB_ICONERROR);
        return FALSE;
    }
    return TRUE;
}

void CCameraManager::DisconnectCamera(int i) {
    if (i < 0 || i >= MAX_CAMERAS) return;

    if (m_hGrabThreads[i]) {
        m_bThreadStopFlags[i] = TRUE;
        WaitForSingleObject(m_hGrabThreads[i], 3000);
        CloseHandle(m_hGrabThreads[i]); m_hGrabThreads[i] = nullptr;
    }
    try {
        if (m_Cameras[i].IsGrabbing()) m_Cameras[i].StopGrabbing();
        if (m_Cameras[i].IsOpen()) m_Cameras[i].Close();
        if (m_Cameras[i].IsPylonDeviceAttached()) m_Cameras[i].DetachDevice();
    }
    catch (...) {}
    if (m_Comms[i].IsConnected()) m_Comms[i].Disconnect();
    if (m_Detectors[i]) m_Detectors[i].release();
    m_bThreadStopFlags[i] = FALSE;
}
void CCameraManager::DisconnectAll() { for (int i = 0;i < MAX_CAMERAS;++i) DisconnectCamera(i); }
BOOL CCameraManager::IsCameraConnected(int i) {
    return (i >= 0 && i < MAX_CAMERAS) && m_Cameras[i].IsPylonDeviceAttached() && m_Comms[i].IsConnected() && m_hGrabThreads[i] != nullptr;
}
void CCameraManager::TriggerManualCapture(int i) { if (i >= 0 && i < MAX_CAMERAS) m_bManualTrigger[i] = TRUE; }
void CCameraManager::UpdateMotionSettings(int i, BOOL en, int th) {
    if (i < 0 || i >= MAX_CAMERAS) return;
    // --- 수정된 부분 (GrabThreadParams 접근 방식 확인) ---
    // GrabThreadParams가 배열이 아니라면 직접 접근 가능
    if (m_ThreadParams[i].pCamera != nullptr) // 스레드 파라미터가 유효한지 확인 후 접근
    {
        m_ThreadParams[i].config.bMotionEnabled = en;
        m_ThreadParams[i].config.nMotionThreshold = th;
    }
    // --- 수정 끝 ---
}

// Grab thread
UINT __cdecl CCameraManager::GrabThread(LPVOID p) {
    auto* prm = static_cast<GrabThreadParams*>(p);
    if (!prm) return 1;

    const int cam = prm->nCameraIndex;
    auto* pCam = prm->pCamera;
    auto* pComm = static_cast<CTcpCommunicator*>(prm->pCommunicator);
    auto* pDet = static_cast<cv::BackgroundSubtractorMOG2*>(prm->pDetector);
    auto* lk = prm->pCommLock;
    // --- 수정된 부분 (config 멤버 직접 사용) ---
    // CameraConfig cfg = prm->config; // 복사본 대신 직접 사용
    // --- 수정 끝 ---


    // MainFrame → manager 접근
    auto* frameWnd = dynamic_cast<CFrameWnd*>(AfxGetMainWnd());
    auto* mainFrm = dynamic_cast<class CMainFrame*>(frameWnd);
    auto* mgr = mainFrm ? mainFrm->GetCameraManager() : nullptr;
    if (!pCam || !pComm || !pDet || !lk || !mgr) {
        if (prm->hNotifyWnd) ::PostMessage(prm->hNotifyWnd, WM_CAMERA_STATUS, cam, (LPARAM)new CString(_T("Thread init error")));
        return 2;
    }

    CPylonImage pimg; CGrabResultPtr grab; CImageFormatConverter cvt; cvt.OutputPixelFormat = PixelType_BGR8packed;

    while (!mgr->m_bThreadStopFlags[cam]) {
        try {
            if (!pCam->RetrieveResult(3000, grab, TimeoutHandling_Return)) { continue; }
            if (!grab.IsValid() || !grab->GrabSucceeded()) continue;

            cvt.Convert(pimg, grab);
            cv::Mat frm((int)pimg.GetHeight(), (int)pimg.GetWidth(), CV_8UC3, (uint8_t*)pimg.GetBuffer());

            // 화면 송출
            if (prm->hNotifyWnd && ::IsWindow(prm->hNotifyWnd)) {
                auto* copy = new cv::Mat(frm.clone());
                if (!::PostMessage(prm->hNotifyWnd, WM_UPDATE_FRAME, cam, (LPARAM)copy)) delete copy;
            }

            // 전송 조건
            BOOL send = FALSE;
            // --- 수정된 부분 (prm->config 직접 사용) ---
            if (prm->config.bMotionEnabled) {
                cv::Mat mask; pDet->apply(frm, mask);
                if (cv::countNonZero(mask) > prm->config.nMotionThreshold) send = TRUE;
            }
            // --- 수정 끝 ---
            if (mgr->m_bManualTrigger[cam]) { send = TRUE; mgr->m_bManualTrigger[cam] = FALSE; }

            if (send && pComm->IsConnected()) {
                DWORD ts = GetTickCount();
                std::vector<uchar> jpg;
                cv::imencode(".jpg", frm, jpg, { cv::IMWRITE_JPEG_QUALITY, 90 });
                CSingleLock lock(lk, TRUE);
                pComm->SendImage(jpg.data(), (int)jpg.size(), ts);
            }

            // 서버로부터 JSON 결과 수신(비동기 poll)
            if (pComm->IsConnected()) {
                nlohmann::json js;
                while (pComm->TryPopResult(js)) { // 큐에 쌓인 결과 모두 처리
                    InspectionResult* r = new InspectionResult{};
                    // JSON → 구조체 매핑
                    r->nCameraIndex = cam;
                    r->sProduct = CString(js.value("product", "").c_str());
                    r->sFinalResult = CString(js.value("final", "").c_str());
                    r->sTime = CString(js.value("time", "").c_str());
                    r->sDefectSummary = CString(js.value("summary", "").c_str());
                    r->dwTimestamp = GetTickCount();

                    if (js.contains("items") && js["items"].is_array()) {
                        for (auto& it : js["items"]) {
                            DefectItem di;
                            di.sCameraType = CString(it.value("cam", "").c_str());
                            di.sDefectType = CString(it.value("type", "").c_str());
                            di.sDefectValue = CString(it.value("value", "").c_str());
                            di.fConfidence = it.value("conf", 0.0f);
                            di.sAdditionalInfo = CString(it.value("info", "").c_str());
                            r->vecDefects.push_back(std::move(di));
                        }
                    }
                    if (prm->hNotifyWnd && ::IsWindow(prm->hNotifyWnd)) {
                        ::PostMessage(prm->hNotifyWnd, WM_INSPECTION_RESULT, 0, (LPARAM)r);
                    }
                    else delete r;
                }
            }

        }
        catch (const GenericException&) { /* continue */ }
    }
    return 0;
}