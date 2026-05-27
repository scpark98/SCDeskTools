// SCRegionCaptureDlg.cpp

#include "pch.h"
#include "SCRegionCaptureDlg.h"
#include "Common/Functions.h"
#include "Common/cursor_helpers.h"

#include <utility>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")

IMPLEMENT_DYNAMIC(CSCRegionCaptureDlg, CSCFrozenOverlayDlg)

BEGIN_MESSAGE_MAP(CSCRegionCaptureDlg, CSCFrozenOverlayDlg)
	ON_WM_SETCURSOR()
	ON_WM_LBUTTONDBLCLK()
END_MESSAGE_MAP()

BOOL CSCRegionCaptureDlg::OnSetCursor(CWnd* /*pWnd*/, UINT /*nHitTest*/, UINT /*message*/)
{
	//base 의 virtual query_cursor 체인이 SetCapture 환경에서 우회될 수 있어 직접 SetCursor.
	HCURSOR hc;
	if (m_phase == Phase::phase_edit)
	{
		POINT pt;
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		hc = ::LoadCursor(NULL, cursor_id_for_hit(hit_test(CPoint(pt))));
	}
	else
	{
		hc = get_thin_cross_cursor();
	}
	::SetCursor(hc);
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

CSCRegionCaptureDlg::HitTarget CSCRegionCaptureDlg::hit_test(CPoint pt) const
{
	if (m_phase != Phase::phase_edit)
		return ht_none;

	const int E = 6;
	const CRect& r = m_edit_rect;

	const bool nL = ::abs(pt.x - r.left)	<= E;
	const bool nR = ::abs(pt.x - r.right)	<= E;
	const bool nT = ::abs(pt.y - r.top)	<= E;
	const bool nB = ::abs(pt.y - r.bottom)	<= E;

	if (nT && nL) return ht_top_left;
	if (nT && nR) return ht_top_right;
	if (nB && nL) return ht_bottom_left;
	if (nB && nR) return ht_bottom_right;
	if (nT && pt.x > r.left && pt.x < r.right) return ht_top;
	if (nB && pt.x > r.left && pt.x < r.right) return ht_bottom;
	if (nL && pt.y > r.top	&& pt.y < r.bottom) return ht_left;
	if (nR && pt.y > r.top	&& pt.y < r.bottom) return ht_right;
	if (r.PtInRect(pt)) return ht_interior;
	return ht_none;
}

LPCTSTR CSCRegionCaptureDlg::cursor_id_for_hit(HitTarget h)
{
	switch (h)
	{
	case ht_top_left:
	case ht_bottom_right:	return IDC_SIZENWSE;
	case ht_top_right:
	case ht_bottom_left:	return IDC_SIZENESW;
	case ht_top:
	case ht_bottom:		return IDC_SIZENS;
	case ht_left:
	case ht_right:			return IDC_SIZEWE;
	case ht_interior:		return IDC_SIZEALL;
	default:				return IDC_CROSS;
	}
}

void CSCRegionCaptureDlg::commit_capture()
{
	m_edit_rect.NormalizeRect();
	if (m_edit_rect.Width() > 0 && m_edit_rect.Height() > 0)
	{
		m_picked = true;
		m_picked_rect_screen = m_edit_rect;
		m_picked_rect_screen.OffsetRect(m_virtual_screen.left, m_virtual_screen.top);
	}
	finish();
}

void CSCRegionCaptureDlg::on_mouse_down(UINT /*nFlags*/, CPoint point)
{
	if (m_phase == Phase::phase_dragging)
	{
		m_dragging = true;
		m_drag_anchor_client = point;
		m_drag_cursor_client = point;
		Invalidate(FALSE);
		return;
	}

	//phase_edit
	HitTarget h = hit_test(point);
	m_edit_grab = h;
	if (h == ht_interior)
		m_edit_grab_offset = CPoint(point.x - m_edit_rect.left, point.y - m_edit_rect.top);
	Invalidate(FALSE);
}

void CSCRegionCaptureDlg::on_mouse_move(UINT /*nFlags*/, CPoint point)
{
	if (m_phase == Phase::phase_dragging)
	{
		if (!m_dragging)
			return;
		m_drag_cursor_client = point;
		Invalidate(FALSE);
		return;
	}

	//phase_edit
	if (m_edit_grab == ht_none)
		return;

	switch (m_edit_grab)
	{
	case ht_top_left:		m_edit_rect.left = point.x; m_edit_rect.top = point.y; break;
	case ht_top:			m_edit_rect.top = point.y; break;
	case ht_top_right:		m_edit_rect.right = point.x; m_edit_rect.top = point.y; break;
	case ht_left:			m_edit_rect.left = point.x; break;
	case ht_right:			m_edit_rect.right = point.x; break;
	case ht_bottom_left:	m_edit_rect.left = point.x; m_edit_rect.bottom = point.y; break;
	case ht_bottom:		m_edit_rect.bottom = point.y; break;
	case ht_bottom_right:	m_edit_rect.right = point.x; m_edit_rect.bottom = point.y; break;
	case ht_interior:
	{
		const int w = m_edit_rect.Width();
		const int h = m_edit_rect.Height();
		m_edit_rect.left = point.x - m_edit_grab_offset.x;
		m_edit_rect.top = point.y - m_edit_grab_offset.y;
		m_edit_rect.right = m_edit_rect.left + w;
		m_edit_rect.bottom = m_edit_rect.top + h;
		break;
	}
	default: break;
	}
	Invalidate(FALSE);
}

void CSCRegionCaptureDlg::on_mouse_up(UINT /*nFlags*/, CPoint point)
{
	if (m_phase == Phase::phase_dragging)
	{
		if (!m_dragging)
			return;
		m_dragging = false;
		m_drag_cursor_client = point;

		CRect sel = current_selection_client();
		if (sel.Width() <= 0 || sel.Height() <= 0)
		{
			finish();
			return;
		}

		const bool shift_held = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
		if (shift_held)
		{
			m_phase = Phase::phase_edit;
			m_edit_rect = sel;
			m_edit_grab = ht_none;
			Invalidate(FALSE);
			return;
		}

		m_picked = true;
		m_picked_rect_screen = sel;
		m_picked_rect_screen.OffsetRect(m_virtual_screen.left, m_virtual_screen.top);
		finish();
		return;
	}

	//phase_edit
	m_edit_grab = ht_none;
	m_edit_rect.NormalizeRect();
	Invalidate(FALSE);
}

void CSCRegionCaptureDlg::OnLButtonDblClk(UINT /*nFlags*/, CPoint point)
{
	if (m_phase == Phase::phase_edit && m_edit_rect.PtInRect(point))
		commit_capture();
}

bool CSCRegionCaptureDlg::on_key_down(UINT nChar)
{
	if (m_phase == Phase::phase_edit)
	{
		if (nChar == VK_RETURN || nChar == VK_SPACE)
		{
			commit_capture();
			return true;
		}

		const int step = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 1 : 8;
		int dx = 0, dy = 0;
		switch (nChar)
		{
		case VK_LEFT:	dx = -step; break;
		case VK_RIGHT:	dx =  step; break;
		case VK_UP:		dy = -step; break;
		case VK_DOWN:	dy =  step; break;
		default:
			return CSCFrozenOverlayDlg::on_key_down(nChar);
		}
		m_edit_rect.OffsetRect(dx, dy);
		Invalidate(FALSE);
		return true;
	}

	//phase_dragging — 방향키로 OS 커서 1px 이동 (정밀 위치 조정).
	int dx = 0, dy = 0;
	switch (nChar)
	{
	case VK_LEFT:	dx = -1; break;
	case VK_RIGHT:	dx =  1; break;
	case VK_UP:		dy = -1; break;
	case VK_DOWN:	dy =  1; break;
	default:
		return CSCFrozenOverlayDlg::on_key_down(nChar);
	}

	POINT pt;
	::GetCursorPos(&pt);
	::SetCursorPos(pt.x + dx, pt.y + dy);
	return true;
}

HCURSOR CSCRegionCaptureDlg::query_cursor(CPoint pt)
{
	if (m_phase == Phase::phase_edit)
		return ::LoadCursor(NULL, cursor_id_for_hit(hit_test(pt)));
	return get_thin_cross_cursor();
}

void CSCRegionCaptureDlg::on_overlay_paint(ID2D1DeviceContext* d2dc)
{
	const int full_w = m_virtual_screen.Width();
	const int full_h = m_virtual_screen.Height();

	const Gdiplus::Color cr_mask		(102, 0,	0,	 0);	//40% black
	const Gdiplus::Color cr_stroke		(255, 65,	105, 225);	//RoyalBlue
	const Gdiplus::Color cr_handle_fill	= Gdiplus::Color::White;
	const Gdiplus::Color cr_label_text	= Gdiplus::Color(255, 212, 212, 212);	//50% white
	const Gdiplus::Color cr_label_outline = Gdiplus::Color::Black;	//draw_text stroke 인자: 글자 외곽 검은선
	const Gdiplus::Color cr_label_shadow= Gdiplus::Color(128, 0, 0, 0);	//50% black

	CRect rc_show;
	bool show = false;
	if (m_phase == Phase::phase_edit)
	{
		rc_show = m_edit_rect;
		rc_show.NormalizeRect();
		show = !rc_show.IsRectEmpty();
	}
	else if (m_dragging)
	{
		rc_show = current_selection_client();
		show = !rc_show.IsRectEmpty();
	}

	if (!show)
	{
		draw_rect(d2dc, CRect(0, 0, full_w, full_h), Gdiplus::Color::Transparent, cr_mask, 0.0f, 0.0f);
		return;
	}

	const int l = rc_show.left;
	const int t = rc_show.top;
	const int r = rc_show.right;
	const int b = rc_show.bottom;

	//ROI 외곽 4 분할 마스크.
	draw_rect(d2dc, CRect(0, 0, full_w, t),			Gdiplus::Color::Transparent, cr_mask, 0.0f, 0.0f);
	draw_rect(d2dc, CRect(0, b, full_w, full_h),	Gdiplus::Color::Transparent, cr_mask, 0.0f, 0.0f);
	draw_rect(d2dc, CRect(0, t, l, b),				Gdiplus::Color::Transparent, cr_mask, 0.0f, 0.0f);
	draw_rect(d2dc, CRect(r, t, full_w, b),			Gdiplus::Color::Transparent, cr_mask, 0.0f, 0.0f);

	//ROI 보더.
	draw_rect(d2dc, CRect(l, t, r, b), cr_stroke, Gdiplus::Color::Transparent, 2.0f, 0.0f);

	if (m_phase == Phase::phase_edit)
	{
		//변 hit-test 는 유지(투명 edge resize) 하되 시각 표시는 4 코너 핸들만 — 모던 크롭툴 추세.
		const int HS = 5;
		auto draw_handle = [&](int cx, int cy)
		{
			draw_rect(d2dc, CRect(cx - HS, cy - HS, cx + HS, cy + HS),
				cr_stroke, cr_handle_fill, 1.5f, 0.0f);
		};
		draw_handle(l, t);
		draw_handle(r, t);
		draw_handle(l, b);
		draw_handle(r, b);
	}

	const int margin = 4;
	const int line_height = 21;		//font_size 14 * line_spacing 1.5 ≈ DWRITE TextMetrics height (Common draw_text 내부)

	WCHAR coord_tl[64];
	swprintf_s(coord_tl, L"(%d, %d)", rc_show.left + m_virtual_screen.left, rc_show.top + m_virtual_screen.top);
	draw_text(d2dc, CRect(l, 0, full_w, t - margin), coord_tl,
		_T("Segoe UI"), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
		cr_label_text, cr_label_outline, cr_label_shadow, Gdiplus::Color::Transparent,
		1.0f, DT_LEFT | DT_BOTTOM);

	const int center_x = (l + r) / 2;
	const int sw = rc_show.Width();
	const int sh = rc_show.Height();
	WCHAR text_size[64];
	if (sh > 0)
		swprintf_s(text_size, L"%d x %d (%.3f:1)", sw, sh, double(sw) / double(sh));
	else
		swprintf_s(text_size, L"%d x %d", sw, sh);
	draw_text(d2dc, CRect(l, t, r, b), text_size,
		_T("Segoe UI"), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
		cr_label_text, cr_label_outline, cr_label_shadow, Gdiplus::Color::Transparent,
		1.0f, DT_CENTER | DT_VCENTER);

	WCHAR coord_br[64];
	swprintf_s(coord_br, L"(%d, %d)", rc_show.right + m_virtual_screen.left, rc_show.bottom + m_virtual_screen.top);
	draw_text(d2dc, CRect(0, b + margin, r, full_h), coord_br,
		_T("Segoe UI"), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
		cr_label_text, cr_label_outline, cr_label_shadow, Gdiplus::Color::Transparent,
		1.0f, DT_RIGHT | DT_TOP);

	LPCWSTR hint = (m_phase == Phase::phase_edit)
		? L"Enter or 더블클릭 : 캡처    ESC or 우클릭 : 취소\n방향키로 이동 가능 (Shift키로 미세 조정 가능)"
		: L"Shift키를 누른 상태에서 영역선택 완료 시 수동 조정모드 가능";
	draw_text(d2dc, CRect(center_x - full_w, b + margin + line_height + margin, center_x + full_w, full_h), hint,
		_T("Segoe UI"), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
		cr_label_text, cr_label_outline, cr_label_shadow, Gdiplus::Color::Transparent,
		1.0f, DT_CENTER | DT_TOP);
}
