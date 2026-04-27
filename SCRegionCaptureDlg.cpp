// SCRegionCaptureDlg.cpp

#include "pch.h"
#include "SCRegionCaptureDlg.h"

#include <utility>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")

IMPLEMENT_DYNAMIC(CSCRegionCaptureDlg, CSCFrozenOverlayDlg)

BEGIN_MESSAGE_MAP(CSCRegionCaptureDlg, CSCFrozenOverlayDlg)
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

BOOL CSCRegionCaptureDlg::OnSetCursor(CWnd* /*pWnd*/, UINT /*nHitTest*/, UINT /*message*/)
{
	//WM_SETCURSOR 직접 처리 — base 의 virtual query_cursor 체인이 우회되는 환경에서도
	//확실히 십자 커서. nHitTest 무시하고 무조건 cross.
	::SetCursor(::LoadCursor(NULL, IDC_CROSS));
	return TRUE;
}

CRect CSCRegionCaptureDlg::current_selection_client() const
{
	int x1 = m_drag_anchor_client.x;
	int y1 = m_drag_anchor_client.y;
	int x2 = m_drag_cursor_client.x;
	int y2 = m_drag_cursor_client.y;

	if (x1 > x2)
		std::swap(x1, x2);
	if (y1 > y2)
		std::swap(y1, y2);

	return CRect(x1, y1, x2, y2);
}

void CSCRegionCaptureDlg::on_mouse_down(UINT /*nFlags*/, CPoint point)
{
	m_dragging = true;
	m_drag_anchor_client = point;
	m_drag_cursor_client = point;
	Invalidate(FALSE);
}

void CSCRegionCaptureDlg::on_mouse_move(UINT /*nFlags*/, CPoint point)
{
	if (!m_dragging)
		return;

	m_drag_cursor_client = point;
	Invalidate(FALSE);
}

void CSCRegionCaptureDlg::on_mouse_up(UINT /*nFlags*/, CPoint point)
{
	if (!m_dragging)
		return;

	m_dragging = false;
	m_drag_cursor_client = point;

	CRect sel = current_selection_client();
	if (sel.Width() > 0 && sel.Height() > 0)
	{
		m_picked = true;
		m_picked_rect_screen = sel;
		m_picked_rect_screen.OffsetRect(m_virtual_screen.left, m_virtual_screen.top);
	}
	finish();
}

bool CSCRegionCaptureDlg::on_key_down(UINT nChar)
{
	//방향키로 커서 1px 단위 이동 — 정밀 위치 조정용. SetCursorPos 가 WM_MOUSEMOVE 를 유발하므로
	//드래그 중이면 on_mouse_move 가 자동으로 selection rect 갱신하고 다시 그림.
	int dx = 0, dy = 0;
	switch (nChar)
	{
	case VK_LEFT:	dx = -1; break;
	case VK_RIGHT: dx =	1; break;
	case VK_UP:	dy = -1; break;
	case VK_DOWN:	dy =	1; break;
	default:
		return CSCFrozenOverlayDlg::on_key_down(nChar);	//ESC 등은 base 가 처리
	}

	POINT pt;
	::GetCursorPos(&pt);
	::SetCursorPos(pt.x + dx, pt.y + dy);
	return true;
}

HCURSOR CSCRegionCaptureDlg::query_cursor(CPoint /*pt*/)
{
	//십자 커서 명시 (window class default 도 IDC_CROSS 지만 base OnSetCursor 가 다른 핸들러로
	//넘겨주는 경로를 차단하기 위해 명시적으로 항상 cross 반환).
	return ::LoadCursor(NULL, IDC_CROSS);
}

void CSCRegionCaptureDlg::on_overlay_paint(ID2D1DeviceContext* d2dc)
{
	const float full_w = float(m_virtual_screen.Width());
	const float full_h = float(m_virtual_screen.Height());

	ComPtr<ID2D1SolidColorBrush> br_mask;
	ComPtr<ID2D1SolidColorBrush> br_stroke;
	d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 0.40f), br_mask.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0x4169E1, 1.00f),	br_stroke.GetAddressOf());	//RoyalBlue

	if (!m_dragging)
	{
		//드래그 시작 전: 화면 전체에 옅은 마스크.
		d2dc->FillRectangle(D2D1::RectF(0, 0, full_w, full_h), br_mask.Get());
		return;
	}

	CRect sel = current_selection_client();
	const float l = float(sel.left);
	const float t = float(sel.top);
	const float r = float(sel.right);
	const float b = float(sel.bottom);

	//선택 영역 외부 4분할 마스크.
	d2dc->FillRectangle(D2D1::RectF(0, 0, full_w,	t),	br_mask.Get());
	d2dc->FillRectangle(D2D1::RectF(0, b, full_w,	full_h), br_mask.Get());
	d2dc->FillRectangle(D2D1::RectF(0, t, l,	b),	br_mask.Get());
	d2dc->FillRectangle(D2D1::RectF(r, t, full_w,	b),	br_mask.Get());

	//선택 보더.
	d2dc->DrawRectangle(D2D1::RectF(l, t, r, b), br_stroke.Get(), 2.0f);

	//크기 텍스트 (선택 영역 가로 중앙).
	ComPtr<IDWriteFactory> dwrite;
	ComPtr<IDWriteTextFormat> tf;
	HRESULT hrf = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
		__uuidof(IDWriteFactory), (IUnknown**)dwrite.GetAddressOf());
	if (SUCCEEDED(hrf))
	{
		dwrite->CreateTextFormat(L"Segoe UI", NULL,
			DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			14.0f, L"", tf.GetAddressOf());
	}
	if (!tf)
		return;

	WCHAR text[64];
	swprintf_s(text, L"%d x %d", sel.Width(), sel.Height());

	tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	ComPtr<ID2D1SolidColorBrush> br_label_back;
	ComPtr<ID2D1SolidColorBrush> br_label_text;
	d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 0.70f), br_label_back.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.00f), br_label_text.GetAddressOf());

	const float label_w = 90.0f;
	const float label_h = 22.0f;
	float lx = (l + r) * 0.5f - label_w * 0.5f;
	float ly = b + 6.0f;
	if (lx < 0)
		lx = 0;
	if (lx + label_w > full_w)
		lx = full_w - label_w;
	if (ly + label_h > full_h)
		ly = b - label_h - 6.0f;

	D2D1_ROUNDED_RECT rr = { D2D1::RectF(lx, ly, lx + label_w, ly + label_h), 4.0f, 4.0f };
	d2dc->FillRoundedRectangle(rr, br_label_back.Get());
	d2dc->DrawText(text, UINT32(wcslen(text)), tf.Get(),
		D2D1::RectF(lx, ly, lx + label_w, ly + label_h),
		br_label_text.Get(),
		D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}
