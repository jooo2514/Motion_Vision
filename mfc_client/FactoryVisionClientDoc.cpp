#include "pch.h"
#include "framework.h"
#ifndef SHARED_HANDLERS
#include "FactoryVisionClient.h"
#endif
#include "FactoryVisionClientDoc.h"
#include <afxdisp.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNCREATE(CFactoryVisionClientDoc, CDocument)

BEGIN_MESSAGE_MAP(CFactoryVisionClientDoc, CDocument)
END_MESSAGE_MAP()

CFactoryVisionClientDoc::CFactoryVisionClientDoc() noexcept {}
CFactoryVisionClientDoc::~CFactoryVisionClientDoc() {}

BOOL CFactoryVisionClientDoc::OnNewDocument()
{
    if (!CDocument::OnNewDocument()) return FALSE;
    return TRUE;
}

void CFactoryVisionClientDoc::Serialize(CArchive& ar)
{
    if (ar.IsStoring()) {}
    else {}
}

#ifdef _DEBUG
void CFactoryVisionClientDoc::AssertValid() const { CDocument::AssertValid(); }
void CFactoryVisionClientDoc::Dump(CDumpContext& dc) const { CDocument::Dump(dc); }
#endif
