// SCFreehandCaptureDlg.cpp

#include "pch.h"
#include "SCFreehandCaptureDlg.h"
#include "Common/cursor_helpers.h"

IMPLEMENT_DYNAMIC(CSCFreehandCaptureDlg, CSCFrozenOverlayDlg)

BEGIN_MESSAGE_MAP(CSCFreehandCaptureDlg, CSCFrozenOverlayDlg)
END_MESSAGE_MAP()

void CSCFreehandCaptureDlg::on_mouse_down(UINT /*nFlags*/, CPoint pt)
{
	m_drawing = true;
	m_path.clear();
	m_path.push_back(pt);
	Invalidate(FALSE);
}

void CSCFreehandCaptureDlg::on_mouse_move(UINT /*nFlags*/, CPoint pt)
{
	if (!m_drawing)
		return;

	//중복 / 매우 근접 점 노이즈 제거 (2px^2 임계).
	if (!m_path.empty())
	{
		const CPoint& last = m_path.back();
		const int dx = pt.x - last.x;
		const int dy = pt.y - last.y;
		if (dx * dx + dy * dy < 4)
			return;
	}
	m_path.push_back(pt);
	Invalidate(FALSE);
}

void CSCFreehandCaptureDlg::on_mouse_up(UINT /*nFlags*/, CPoint pt)
{
	if (!m_drawing)
		return;
	m_drawing = false;

	if (m_path.empty() || m_path.back() != pt)
		m_path.push_back(pt);

	if (m_path.size() < 3)
	{
		finish();
		return;
	}

	//경로를 가상 화면 좌표로 변환 + bounding box 계산.
	m_picked_path_screen.clear();
	m_picked_path_screen.reserve(m_path.size());
	int min_x = INT_MAX, min_y = INT_MAX, max_x = INT_MIN, max_y = INT_MIN;
	for (const CPoint& p : m_path)
	{
		const CPoint sp(p.x + m_virtual_screen.left, p.y + m_virtual_screen.top);
		m_picked_path_screen.push_back(sp);
		if (sp.x < min_x) min_x = sp.x;
		if (sp.y < min_y) min_y = sp.y;
		if (sp.x > max_x) max_x = sp.x;
		if (sp.y > max_y) max_y = sp.y;
	}
	m_picked_bounds_screen.SetRect(min_x, min_y, max_x + 1, max_y + 1);
	m_picked = true;

	finish();
}

HCURSOR CSCFreehandCaptureDlg::query_cursor(CPoint /*pt*/)
{
	return get_thin_cross_cursor();
}

void CSCFreehandCaptureDlg::on_overlay_paint(ID2D1DeviceContext* d2dc)
{
	const float full_w = float(m_virtual_screen.Width());
	const float full_h = float(m_virtual_screen.Height());

	//전체 마스크 (40% 검정) 만 — 경로 점이 부족하면 폴리곤 미생성.
	if (m_path.size() < 2)
	{
		ID2D1SolidColorBrush* pMask = nullptr;
		d2dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.4f), &pMask);
		if (pMask)
		{
			d2dc->FillRectangle(D2D1::RectF(0, 0, full_w, full_h), pMask);
			pMask->Release();
		}
		return;
	}

	ID2D1Factory* pFactory = nullptr;
	d2dc->GetFactory(&pFactory);
	if (!pFactory)
		return;

	//폴리곤 geometry (시각/캡처 모두 자동 닫힘).
	ID2D1PathGeometry* pPoly = nullptr;
	pFactory->CreatePathGeometry(&pPoly);
	if (!pPoly)
		return;

	{
		ID2D1GeometrySink* pSink = nullptr;
		pPoly->Open(&pSink);
		if (pSink)
		{
			pSink->BeginFigure(
				D2D1::Point2F(float(m_path[0].x), float(m_path[0].y)),
				D2D1_FIGURE_BEGIN_FILLED);
			for (size_t i = 1; i < m_path.size(); ++i)
				pSink->AddLine(D2D1::Point2F(float(m_path[i].x), float(m_path[i].y)));
			pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
			pSink->Close();
			pSink->Release();
		}
	}

	//바깥 = 전체 사각형 - 폴리곤 (EXCLUDE).
	ID2D1RectangleGeometry* pFull = nullptr;
	pFactory->CreateRectangleGeometry(D2D1::RectF(0, 0, full_w, full_h), &pFull);
	if (pFull)
	{
		ID2D1PathGeometry* pOutside = nullptr;
		pFactory->CreatePathGeometry(&pOutside);
		if (pOutside)
		{
			ID2D1GeometrySink* pSinkO = nullptr;
			pOutside->Open(&pSinkO);
			if (pSinkO)
			{
				pFull->CombineWithGeometry(pPoly, D2D1_COMBINE_MODE_EXCLUDE, NULL, pSinkO);
				pSinkO->Close();
				pSinkO->Release();

				ID2D1SolidColorBrush* pMask = nullptr;
				d2dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.4f), &pMask);
				if (pMask)
				{
					d2dc->FillGeometry(pOutside, pMask);
					pMask->Release();
				}
			}
			pOutside->Release();
		}
		pFull->Release();
	}

	//폴리곤 stroke (RoyalBlue, 2px).
	ID2D1SolidColorBrush* pStroke = nullptr;
	d2dc->CreateSolidColorBrush(
		D2D1::ColorF(65 / 255.0f, 105 / 255.0f, 225 / 255.0f, 1.0f), &pStroke);
	if (pStroke)
	{
		d2dc->DrawGeometry(pPoly, pStroke, scaled_f(2.0f));
		pStroke->Release();
	}

	pPoly->Release();
}
