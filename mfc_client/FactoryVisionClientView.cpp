#include "pch.h"
#include "framework.h"
#ifndef SHARED_HANDLERS
#include "FactoryVisionClient.h"
#endif
#include "FactoryVisionClientDoc.h"
#include "FactoryVisionClientView.h"
#include "resource.h"
#include "MainFrm.h"
#include <Shlwapi.h>
#include <gdiplus.h>
#include <vector>
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "gdiplus.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNCREATE(CFactoryVisionClientView, CView)

BEGIN_MESSAGE_MAP(CFactoryVisionClientView, CView)
    ON_COMMAND(ID_FILE_PRINT, &CView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_DIRECT, &CView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CView::OnFilePrintPreview)
    ON_WM_ERASEBKGND()
    ON_MESSAGE(WM_UPDATE_FRAME, &CFactoryVisionClientView::OnUpdateFrame)
    ON_MESSAGE(WM_INSPECTION_RESULT, &CFactoryVisionClientView::OnInspectionResult)
    ON_COMMAND(ID_EXPORT_CSV, &CFactoryVisionClientView::OnExportCsv)
END_MESSAGE_MAP()

CFactoryVisionClientView::CFactoryVisionClientView() noexcept
{
    // --- 수정된 부분 (GDI+ Font 생성 시 new 사용 방식 변경) ---
    // Gdiplus::Font 생성자가 실패할 수 있으므로 new 사용 방식을 표준으로 변경하고 nullptr 체크 추가
    m_pFontLarge = ::new Gdiplus::Font(L"Segoe UI", 36, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    m_pFontMedium = ::new Gdiplus::Font(L"Segoe UI", 18, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    m_pFontSmall = ::new Gdiplus::Font(L"Segoe UI", 12, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    m_pFontDefect = ::new Gdiplus::Font(L"Segoe UI", 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    // --- 수정 끝 ---
}
CFactoryVisionClientView::~CFactoryVisionClientView()
{
    delete m_pFontLarge; delete m_pFontMedium; delete m_pFontSmall; delete m_pFontDefect;
}

BOOL CFactoryVisionClientView::PreCreateWindow(CREATESTRUCT& cs)
{
    cs.style &= ~WS_BORDER;
    cs.lpszClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
        ::LoadCursor(nullptr, IDC_ARROW), reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1), nullptr);
    return CView::PreCreateWindow(cs);
}

void CFactoryVisionClientView::OnDraw(CDC* pDC)
{
    CFactoryVisionClientDoc* pDoc = GetDocument(); ASSERT_VALID(pDoc); if (!pDoc) return;

    CRect rc; GetClientRect(&rc); if (rc.Width() <= 0 || rc.Height() <= 0) return;
    CMemDC memDC(*pDC, this); CDC* d = &memDC.GetDC();

    Gdiplus::Graphics g(d->GetSafeHdc());
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

    Gdiplus::SolidBrush bg(Gdiplus::Color(255, 30, 30, 32));
    g.FillRectangle(&bg, 0, 0, rc.Width(), rc.Height());

    Gdiplus::RectF rVideo(0.0f, 0.0f, static_cast<Gdiplus::REAL>(rc.Width()) * 0.75f, static_cast<Gdiplus::REAL>(rc.Height()));
    Gdiplus::RectF rDash(rVideo.Width, 0.0f, static_cast<Gdiplus::REAL>(rc.Width()) * 0.25f, static_cast<Gdiplus::REAL>(rc.Height()));

    DrawVideo(g, rVideo);
    DrawDashboard(g, rDash);
}

void CFactoryVisionClientView::DrawVideo(Gdiplus::Graphics& g, const Gdiplus::RectF& r)
{
    Gdiplus::SolidBrush black(Gdiplus::Color(255, 0, 0, 0));
    g.FillRectangle(&black, r);

    cv::Mat frame;
    {
        CSingleLock lock(&m_csFrame[m_nActiveCameraIndex], TRUE);
        if (!m_CurrentFrames[m_nActiveCameraIndex].empty()) frame = m_CurrentFrames[m_nActiveCameraIndex].clone();
    }

    if (!frame.empty()) DrawBitmap(g, frame, r, true);
    else {
        Gdiplus::StringFormat f; f.SetAlignment(Gdiplus::StringAlignmentCenter); f.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush t(Gdiplus::Color(180, 150, 150, 150));
        if (m_pFontLarge) DrawTextWithShadow(g, _T("NO SIGNAL"), m_pFontLarge, r, &t, &f);
    }
}

void CFactoryVisionClientView::DrawDashboard(Gdiplus::Graphics& g, const Gdiplus::RectF& r)
{
    Gdiplus::SolidBrush dash(Gdiplus::Color(255, 45, 45, 48));
    g.FillRectangle(&dash, r);

    DefectData latest; bool has = false;
    { CSingleLock l(&m_csResult, TRUE); if (!m_DefectHistory.empty()) { latest = m_DefectHistory.front(); has = true; } }

    Gdiplus::REAL margin = 10.0f;
    Gdiplus::RectF inner(r.X + margin, r.Y + margin, r.Width - 2 * margin, r.Height - 2 * margin);
    Gdiplus::REAL titleH = 40.0f, summaryH = 60.0f;

    Gdiplus::SolidBrush white(Gdiplus::Color(255, 240, 240, 240));
    Gdiplus::StringFormat titleFmt; titleFmt.SetAlignment(Gdiplus::StringAlignmentCenter); titleFmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
    if (m_pFontMedium) DrawTextWithShadow(g, _T("실시간 검사 결과"), m_pFontMedium, Gdiplus::RectF(inner.X, inner.Y, inner.Width, titleH), &white, &titleFmt);

    if (has)
    {
        const auto& res = latest.result;
        const bool isNG = (res.sFinalResult.CompareNoCase(_T("불량")) == 0);

        Gdiplus::SolidBrush resBrush(isNG ? Gdiplus::Color(200, 255, 80, 80) : Gdiplus::Color(200, 80, 255, 80));
        Gdiplus::StringFormat center; center.SetAlignment(Gdiplus::StringAlignmentCenter); center.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF rRes(inner.X, inner.Y + titleH, inner.Width, summaryH);
        if (m_pFontLarge) DrawTextWithShadow(g, res.sFinalResult, m_pFontLarge, rRes, &resBrush, &center);

        CString info; info.Format(_T("품목: %s    시각: %s"), res.sProduct.GetString(), res.sTime.GetString());
        if (m_pFontSmall) DrawTextWithShadow(g, info, m_pFontSmall, Gdiplus::RectF(inner.X, rRes.GetBottom() + 2, inner.Width, 22), &white, &center);
        if (!res.sDefectSummary.IsEmpty())
            if (m_pFontSmall) DrawTextWithShadow(g, _T("요약: ") + res.sDefectSummary, m_pFontSmall, Gdiplus::RectF(inner.X, rRes.GetBottom() + 25, inner.Width, 22), &white, &center);

        Gdiplus::RectF rList(inner.X, rRes.GetBottom() + 50, inner.Width, inner.Height - (rRes.GetBottom() - inner.Y) - 60);
        Gdiplus::StringFormat left; left.SetAlignment(Gdiplus::StringAlignmentNear); left.SetLineAlignment(Gdiplus::StringAlignmentNear);
        Gdiplus::SolidBrush ng(Gdiplus::Color(255, 255, 100, 100)), ok(Gdiplus::Color(255, 100, 255, 100));

        Gdiplus::REAL y = rList.Y, lh = 20.0f;
        for (const auto& d : res.vecDefects)
        {
            if (y + lh > rList.GetBottom()) break;
            CString line; line.Format(_T("[%s] %s: %s (%.0f%%) %s"),
                d.sCameraType.GetString(), d.sDefectType.GetString(), d.sDefectValue.GetString(),
                d.fConfidence * 100.0f,
                d.sAdditionalInfo.IsEmpty() ? CString(_T("")) : (_T("- ") + d.sAdditionalInfo));
            if (m_pFontDefect) DrawTextWithShadow(g, line, m_pFontDefect, Gdiplus::RectF(rList.X, y, rList.Width, lh),
                (d.sDefectValue.Find(_T("불량")) != -1) ? &ng : &ok, &left);
            y += lh;
        }
    }
    else
    {
        Gdiplus::SolidBrush green(Gdiplus::Color(200, 80, 255, 80));
        Gdiplus::StringFormat c; c.SetAlignment(Gdiplus::StringAlignmentCenter); c.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        if (m_pFontLarge) DrawTextWithShadow(g, _T("SYSTEM NORMAL"), m_pFontLarge, Gdiplus::RectF(inner.X, inner.Y + titleH, inner.Width, inner.Height - titleH), &green, &c);
    }
}

void CFactoryVisionClientView::DrawBitmap(Gdiplus::Graphics& g, const cv::Mat& img, const Gdiplus::RectF& r, bool keepAR)
{
    if (img.empty() || r.Width <= 0 || r.Height <= 0) return;
    if (img.type() != CV_8UC3 && img.type() != CV_8UC1) return;

    cv::Mat displayImg;
    if (img.type() == CV_8UC1)
        cv::cvtColor(img, displayImg, cv::COLOR_GRAY2BGR);
    else
        displayImg = img;

    Gdiplus::Bitmap bmp(displayImg.cols, displayImg.rows, displayImg.step, PixelFormat24bppRGB, (BYTE*)displayImg.data);
    Gdiplus::RectF rd = r;
    if (keepAR) {
        Gdiplus::REAL rc = r.Width / r.Height;
        Gdiplus::REAL ri = (displayImg.cols > 0 && displayImg.rows > 0) ? (static_cast<Gdiplus::REAL>(displayImg.cols) / static_cast<Gdiplus::REAL>(displayImg.rows)) : 1.0f;
        if (rc > ri) { rd.Width = ri * r.Height; rd.X = r.X + (r.Width - rd.Width) / 2.0f; }
        else { rd.Height = r.Width / ri;  rd.Y = r.Y + (r.Height - rd.Height) / 2.0f; }
    }
    g.DrawImage(&bmp, rd);
}

void CFactoryVisionClientView::DrawTextWithShadow(Gdiplus::Graphics& g, const CString& t, Gdiplus::Font* f,
    const Gdiplus::RectF& r, const Gdiplus::Brush* b, Gdiplus::StringFormat* fmt)
{
    if (!f || !b || !fmt) return;

    Gdiplus::SolidBrush sh(Gdiplus::Color(128, 0, 0, 0));
    auto rr = r; rr.X += 2; rr.Y += 2;
    g.DrawString(t, -1, f, rr, fmt, &sh);
    g.DrawString(t, -1, f, r, fmt, b);
}

BOOL CFactoryVisionClientView::OnEraseBkgnd(CDC*) { return TRUE; }

void CFactoryVisionClientView::SetActiveCamera(int idx)
{
    if (idx >= 0 && idx < MAX_CAMERAS && m_nActiveCameraIndex != idx) { m_nActiveCameraIndex = idx; Invalidate(FALSE); }
}

LRESULT CFactoryVisionClientView::OnUpdateFrame(WPARAM w, LPARAM l)
{
    int cam = (int)w; cv::Mat* p = (cv::Mat*)l;
    if (!p || cam < 0 || cam >= MAX_CAMERAS || p->empty()) { delete p; return 0; }

    { CSingleLock lock(&m_csFrame[cam], TRUE); m_CurrentFrames[cam] = p->clone(); }
    if (CWnd* pMain = AfxGetMainWnd())
    {
        auto* pFrm = dynamic_cast<CMainFrame*>(pMain);
        // --- 수정된 부분 (GetLivePanel() 사용 확인, MainFrm.h에 선언 필요) ---
        if (pFrm && pFrm->GetLivePanel()) pFrm->GetLivePanel()->OnNewFrame(cam, *p);
        // --- 수정 끝 ---
    }

    delete p;
    if (cam == m_nActiveCameraIndex) Invalidate(FALSE);
    return 0;
}

LRESULT CFactoryVisionClientView::OnInspectionResult(WPARAM, LPARAM l)
{
    InspectionResult* r = (InspectionResult*)l;
    if (!r) { return 0; }

    DefectData data; data.result = *r;
    {
        CSingleLock lk(&m_csFrame[r->nCameraIndex], TRUE);
        if (!m_CurrentFrames[r->nCameraIndex].empty()) data.matImage = m_CurrentFrames[r->nCameraIndex].clone();
    }

    {
        CSingleLock lk(&m_csResult, TRUE);
        m_DefectHistory.push_front(data);
        while (m_DefectHistory.size() > MAX_DEFECT_HISTORY) m_DefectHistory.pop_back();
    }

    if (r->sFinalResult.CompareNoCase(_T("불량")) == 0)
    {
        MessageBeep(MB_ICONHAND);
        CString imgPath; if (SaveJpegToFile(data.matImage, imgPath))
            ShellExecute(NULL, L"open", imgPath, NULL, NULL, SW_SHOWNORMAL);
    }

    delete r;
    Invalidate(FALSE);
    return 0;
}

void CFactoryVisionClientView::OnExportCsv()
{
    CFileDialog dlg(FALSE, _T("csv"), _T("alarm_history.csv"),
        OFN_OVERWRITEPROMPT, _T("CSV Files (*.csv)|*.csv||"), this);
    if (dlg.DoModal() != IDOK) return;
    if (ExportHistoryToCsv(dlg.GetPathName())) AfxMessageBox(_T("CSV 저장 완료"));
    else AfxMessageBox(_T("CSV 저장 실패"), MB_ICONERROR);
}

bool CFactoryVisionClientView::ExportHistoryToCsv(const CString& filePath)
{
    FILE* fp = nullptr; _wfopen_s(&fp, filePath, L"wt, ccs=UTF-8"); if (!fp) return false;
    fwprintf(fp, L"product,final,time,summary,cam,type,value,conf,info\n");

    CSingleLock lk(&m_csResult, TRUE);
    for (auto it = m_DefectHistory.crbegin(); it != m_DefectHistory.crend(); ++it)
    {
        const auto& d = *it;
        const auto& r = d.result;
        if (r.vecDefects.empty())
        {
            fwprintf(fp, L"%s,%s,%s,%s,,,,,\n",
                r.sProduct.GetString(), r.sFinalResult.GetString(),
                r.sTime.GetString(), r.sDefectSummary.GetString());
        }
        else
        {
            for (const auto& item : r.vecDefects)
            {
                fwprintf(fp, L"%s,%s,%s,%s,%s,%s,%s,%.3f,%s\n",
                    r.sProduct.GetString(), r.sFinalResult.GetString(),
                    r.sTime.GetString(), r.sDefectSummary.GetString(),
                    item.sCameraType.GetString(), item.sDefectType.GetString(),
                    item.sDefectValue.GetString(), item.fConfidence,
                    item.sAdditionalInfo.GetString());
            }
        }
    }
    fclose(fp);
    return true;
}

int CFactoryVisionClientView::ReadJpegQualityFromIni() const
{
    TCHAR path[MAX_PATH] = { 0 }; GetModuleFileName(NULL, path, MAX_PATH);
    PathRemoveFileSpec(path); PathAppend(path, _T("config.ini")); CString ini = path;
    return GetPrivateProfileInt(_T("Panel"), _T("JpegQuality"), 92, ini);
}

bool CFactoryVisionClientView::SaveJpegToFile(const cv::Mat& img, CString& outPath)
{
    if (img.empty()) return false;

    TCHAR exe[MAX_PATH] = { 0 }; GetModuleFileName(NULL, exe, MAX_PATH);
    PathRemoveFileSpec(exe); PathAppend(exe, _T("images"));
    CreateDirectory(exe, nullptr);

    SYSTEMTIME st; GetLocalTime(&st);
    CString name; name.Format(_T("%04d%02d%02d_%02d%02d%02d_%03d_cam%d.jpg"),
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, m_nActiveCameraIndex + 1);

    CString full = exe; PathAppend(full.GetBuffer(MAX_PATH), name); full.ReleaseBuffer();

    int q = ReadJpegQualityFromIni();
    std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, q };
    std::vector<uchar> jpg;
    if (!cv::imencode(".jpg", img, jpg, params)) return false;

    FILE* fp = nullptr; _wfopen_s(&fp, full, L"wb"); if (!fp) return false;
    fwrite(jpg.data(), 1, jpg.size(), fp); fclose(fp);
    outPath = full; return true;
}

BOOL CFactoryVisionClientView::OnPreparePrinting(CPrintInfo* p) { return DoPreparePrinting(p); }
void CFactoryVisionClientView::OnBeginPrinting(CDC*, CPrintInfo*) {}
void CFactoryVisionClientView::OnEndPrinting(CDC*, CPrintInfo*) {}

#ifdef _DEBUG
void CFactoryVisionClientView::AssertValid() const { CView::AssertValid(); }
void CFactoryVisionClientView::Dump(CDumpContext& dc) const { CView::Dump(dc); }
CFactoryVisionClientDoc* CFactoryVisionClientView::GetDocument() const
{
    ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CFactoryVisionClientDoc))); return (CFactoryVisionClientDoc*)m_pDocument;
}
#endif