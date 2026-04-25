// SCCaptureOverlayDlg.cpp

#include "pch.h"
#include "SCCaptureOverlayDlg.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

IMPLEMENT_DYNAMIC(CSCCaptureOverlayDlg, CDialog)

BEGIN_MESSAGE_MAP(CSCCaptureOverlayDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_RBUTTONDOWN()
	ON_WM_KEYDOWN()
END_MESSAGE_MAP()

CSCCaptureOverlayDlg::CSCCaptureOverlayDlg() : CDialog()
{
}

CSCCaptureOverlayDlg::~CSCCaptureOverlayDlg()
{
	if (m_frozen_hbmp)
	{
		::DeleteObject(m_frozen_hbmp);
		m_frozen_hbmp = NULL;
	}
}

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
		//Win10+ 의 보이지 않는 그림자 영역을 빼고 정확한 보이는 rect 획득.
		//실패 시 GetWindowRect 로 fallback.
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

		//cloaked (UWP background, 가상 데스크톱 다른 페이지 등) 제외
		BOOL cloaked = FALSE;
		if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
			return TRUE;

		RECT rc = {};
		if (!query_window_rect(hwnd, &rc))
			return TRUE;

		//rect 가 비정상이거나 비어있으면 skip
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

bool CSCCaptureOverlayDlg::create(CWnd* parent)
{
	//1) 가상 스크린 (모든 모니터) 을 한 번에 덮을 사각형 산출.
	m_virtual_screen.left   = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
	m_virtual_screen.top    = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
	m_virtual_screen.right  = m_virtual_screen.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
	m_virtual_screen.bottom = m_virtual_screen.top  + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

	//2) 프레임 없는 popup 클래스. 십자 커서로 고정.
	LPCTSTR wnd_class = ::AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_CROSS));

	BOOL ok = CreateEx(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		wnd_class,
		_T("SCCaptureOverlay"),
		WS_POPUP,
		m_virtual_screen.left, m_virtual_screen.top,
		m_virtual_screen.Width(), m_virtual_screen.Height(),
		parent ? parent->GetSafeHwnd() : NULL,
		NULL,
		NULL);

	if (!ok)
		return false;

	//우리 자신은 다음 캡처 사이클에서도 캡처되지 않게 (혹시 또 부르더라도).
	::SetWindowDisplayAffinity(m_hWnd, WDA_EXCLUDEFROMCAPTURE);

	//3) 화면 프리즈 캡처 (보이기 전에 수행해야 자기 자신이 들어가지 않음).
	if (!capture_virtual_screen_to_d2())
	{
		DestroyWindow();
		return false;
	}

	//4) 표시 + 마우스 캡처.
	ShowWindow(SW_SHOW);
	SetForegroundWindow();
	SetCapture();

	update_target_under_cursor();
	return true;
}

bool CSCCaptureOverlayDlg::capture_virtual_screen_to_d2()
{
	const int w = m_virtual_screen.Width();
	const int h = m_virtual_screen.Height();
	if (w <= 0 || h <= 0)
		return false;

	HDC hdc_screen = ::GetDC(NULL);
	HDC hdc_mem    = ::CreateCompatibleDC(hdc_screen);

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth       = w;
	bmi.bmiHeader.biHeight      = -h;	//top-down
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* bits = nullptr;
	HBITMAP hbmp = ::CreateDIBSection(hdc_mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
	HGDIOBJ old_bmp = ::SelectObject(hdc_mem, hbmp);

	//CAPTUREBLT 플래그: layered window 까지 합성된 결과를 받아온다.
	BOOL blt_ok = ::BitBlt(hdc_mem, 0, 0, w, h,
		hdc_screen, m_virtual_screen.left, m_virtual_screen.top,
		SRCCOPY | CAPTUREBLT);

	HRESULT hr = E_FAIL;

	if (blt_ok && bits)
	{
		//BitBlt 결과는 BGRX (alpha=0). PBGRA 로 업로드하기 위해 alpha 를 0xFF 로 채움.
		//폴백 경로에서 클립보드로 보낼 때도 alpha 0xFF 가 필요하므로 동일 버퍼에서 처리.
		BYTE* p = static_cast<BYTE*>(bits);
		const int total = w * h;
		for (int i = 0; i < total; ++i)
			p[i * 4 + 3] = 0xFF;

		hr = m_d2.init(m_hWnd, w, h);
		if (SUCCEEDED(hr))
			hr = m_frozen.load(m_d2.get_WICFactory(), m_d2.get_d2dc(), bits, w, h, 4);
	}

	//hbmp 는 ~Dlg 에서 해제할 때까지 유지. 폴백 시 sub-region BitBlt 의 source 로 사용.
	::SelectObject(hdc_mem, old_bmp);
	::DeleteDC(hdc_mem);
	::ReleaseDC(NULL, hdc_screen);

	if (SUCCEEDED(hr) && m_frozen.is_valid())
	{
		m_frozen_hbmp = hbmp;
		return true;
	}

	::DeleteObject(hbmp);
	return false;
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

void CSCCaptureOverlayDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	update_target_under_cursor();
	CDialog::OnMouseMove(nFlags, point);
}

void CSCCaptureOverlayDlg::OnLButtonDown(UINT /*nFlags*/, CPoint /*point*/)
{
	if (m_target_hwnd && !m_target_rect_screen.IsRectEmpty())
	{
		m_picked = true;
		m_picked_rect_screen = m_target_rect_screen;
		m_picked_hwnd = m_target_hwnd;
	}
	finish(m_picked);
}

void CSCCaptureOverlayDlg::OnRButtonDown(UINT /*nFlags*/, CPoint /*point*/)
{
	finish(false);
}

void CSCCaptureOverlayDlg::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_ESCAPE)
	{
		finish(false);
		return;
	}
	CDialog::OnKeyDown(nChar, nRepCnt, nFlags);
}

BOOL CSCCaptureOverlayDlg::PreTranslateMessage(MSG* pMsg)
{
	//ESC 가 OnCancel → EndDialog 로 가지 않게 우리가 먼저 흡수.
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ESCAPE)
	{
		finish(false);
		return TRUE;
	}
	return CDialog::PreTranslateMessage(pMsg);
}

void CSCCaptureOverlayDlg::finish(bool picked)
{
	m_picked = picked;
	if (::GetCapture() == m_hWnd)
		::ReleaseCapture();
	if (GetSafeHwnd())
		DestroyWindow();
}

BOOL CSCCaptureOverlayDlg::OnEraseBkgnd(CDC* /*pDC*/)
{
	//전체를 OnPaint 의 D2D 로 그리므로 GDI 지우기 비활성.
	return TRUE;
}

void CSCCaptureOverlayDlg::OnPaint()
{
	CPaintDC dc(this);

	ID2D1DeviceContext* d2dc = m_d2.get_d2dc();
	if (!d2dc)
		return;

	d2dc->BeginDraw();
	d2dc->SetTransform(D2D1::Matrix3x2F::Identity());

	//프리즈된 배경 (가상 스크린 좌상단 == 오버레이 클라이언트 0,0)
	if (m_frozen.is_valid())
		m_frozen.draw(d2dc, 0, 0);

	if (!m_target_rect_screen.IsRectEmpty())
	{
		//screen coord → overlay client coord 로 평행이동
		D2D1_RECT_F r = D2D1::RectF(
			float(m_target_rect_screen.left   - m_virtual_screen.left),
			float(m_target_rect_screen.top    - m_virtual_screen.top),
			float(m_target_rect_screen.right  - m_virtual_screen.left),
			float(m_target_rect_screen.bottom - m_virtual_screen.top));

		ComPtr<ID2D1SolidColorBrush> br_stroke;
		ComPtr<ID2D1SolidColorBrush> br_fill;
		//RoyalBlue 강조: 안쪽 18% fill + 3px stroke. 추가 픽셀을 거의 안 늘리면서 시각 변화 명확.
		d2dc->CreateSolidColorBrush(D2D1::ColorF(0x4169E1, 1.00f), br_stroke.GetAddressOf());
		d2dc->CreateSolidColorBrush(D2D1::ColorF(0x4169E1, 0.18f), br_fill.GetAddressOf());

		d2dc->FillRectangle(r, br_fill.Get());
		d2dc->DrawRectangle(r, br_stroke.Get(), 3.0f);
	}

	HRESULT hr = d2dc->EndDraw();
	if (SUCCEEDED(hr))
		m_d2.get_swapchain()->Present(0, 0);
	else if (hr == D2DERR_RECREATE_TARGET)
		m_d2.handle_device_lost();
}
