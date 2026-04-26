// SCCaptureOverlayDlg.cpp

#include "pch.h"
#include "SCCaptureOverlayDlg.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

IMPLEMENT_DYNAMIC(CSCCaptureOverlayDlg, CSCFrozenOverlayDlg)

BEGIN_MESSAGE_MAP(CSCCaptureOverlayDlg, CSCFrozenOverlayDlg)
END_MESSAGE_MAP()

//커서 아래 top-level 윈도우를 EnumWindows 로 찾는다.
//이유: 우리 오버레이가 topmost 라 WindowFromPoint 가 항상 우리만 반환.
//     EnumWindows 는 z-order 순(top→bottom) 이라 첫 매치가 우리 다음 위에 있는 창.
namespace
{
	struct EnumCtx
	{
		POINT pt = {};
		HWND  skip = NULL;
		HWND  found = NULL;
		RECT  found_rect = {};
	};

	bool query_window_rect(HWND hwnd, RECT* out)
	{
		//Win10+ 의 보이지 않는 그림자 영역을 빼고 정확한 보이는 rect 획득. 실패 시 GetWindowRect.
		if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, out, sizeof(RECT))))
			return true;
		return ::GetWindowRect(hwnd, out) != FALSE;
	}

	BOOL CALLBACK enum_top_level_proc(HWND hwnd, LPARAM lp)
	{
		auto* ctx = reinterpret_cast<EnumCtx*>(lp);

		if (hwnd == ctx->skip)
			return TRUE;
		if (!::IsWindowVisible(hwnd))
			return TRUE;
		if (::IsIconic(hwnd))
			return TRUE;

		BOOL cloaked = FALSE;
		if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
			return TRUE;

		RECT rc = {};
		if (!query_window_rect(hwnd, &rc))
			return TRUE;
		if (rc.right <= rc.left || rc.bottom <= rc.top)
			return TRUE;

		if (::PtInRect(&rc, ctx->pt))
		{
			ctx->found = hwnd;
			ctx->found_rect = rc;
			return FALSE;	//z-order 첫 매치에서 종료
		}
		return TRUE;
	}
}

void CSCCaptureOverlayDlg::update_target_under_cursor()
{
	POINT pt;
	::GetCursorPos(&pt);

	EnumCtx ctx;
	ctx.pt = pt;
	ctx.skip = m_hWnd;
	::EnumWindows(enum_top_level_proc, reinterpret_cast<LPARAM>(&ctx));

	HWND new_target = ctx.found;
	if (new_target == m_target_hwnd)
		return;

	m_target_hwnd = new_target;
	if (new_target)
		m_target_rect_screen = ctx.found_rect;
	else
		m_target_rect_screen.SetRectEmpty();

	Invalidate(FALSE);
}

void CSCCaptureOverlayDlg::on_mouse_move(UINT /*nFlags*/, CPoint /*point*/)
{
	update_target_under_cursor();
}

void CSCCaptureOverlayDlg::on_mouse_down(UINT /*nFlags*/, CPoint /*point*/)
{
	if (m_target_hwnd && !m_target_rect_screen.IsRectEmpty())
	{
		m_picked = true;
		m_picked_rect_screen = m_target_rect_screen;
		m_picked_hwnd = m_target_hwnd;
	}
	finish();
}

void CSCCaptureOverlayDlg::on_overlay_paint(ID2D1DeviceContext* d2dc)
{
	if (m_target_rect_screen.IsRectEmpty())
		return;

	//screen coord → overlay client coord (virtual screen 좌상단 == 0,0)
	D2D1_RECT_F r = D2D1::RectF(
		float(m_target_rect_screen.left   - m_virtual_screen.left),
		float(m_target_rect_screen.top    - m_virtual_screen.top),
		float(m_target_rect_screen.right  - m_virtual_screen.left),
		float(m_target_rect_screen.bottom - m_virtual_screen.top));

	ComPtr<ID2D1SolidColorBrush> br_stroke;
	ComPtr<ID2D1SolidColorBrush> br_fill;
	//RoyalBlue 강조: 안쪽 18% fill + 3px stroke. 픽셀 거의 안 늘리면서 시각 변화 명확.
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0x4169E1, 1.00f), br_stroke.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0x4169E1, 0.18f), br_fill.GetAddressOf());

	d2dc->FillRectangle(r, br_fill.Get());
	d2dc->DrawRectangle(r, br_stroke.Get(), 3.0f);
}
