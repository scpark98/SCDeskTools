// SCFrozenOverlayDlg.cpp

#include "pch.h"
#include "SCFrozenOverlayDlg.h"

BEGIN_MESSAGE_MAP(CSCFrozenOverlayDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_RBUTTONDOWN()
	ON_WM_KEYDOWN()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

CSCFrozenOverlayDlg::CSCFrozenOverlayDlg() : CDialog()
{
}

CSCFrozenOverlayDlg::~CSCFrozenOverlayDlg()
{
	if (m_frozen_hbmp)
	{
		::DeleteObject(m_frozen_hbmp);
		m_frozen_hbmp = NULL;
	}
}

bool CSCFrozenOverlayDlg::create(CWnd* parent)
{
	m_virtual_screen.left   = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
	m_virtual_screen.top    = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
	m_virtual_screen.right  = m_virtual_screen.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
	m_virtual_screen.bottom = m_virtual_screen.top  + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

	LPCTSTR wnd_class = ::AfxRegisterWndClass(0, ::LoadCursor(NULL, IDC_CROSS));

	BOOL ok = CreateEx(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		wnd_class,
		_T("SCFrozenOverlay"),
		WS_POPUP,
		m_virtual_screen.left, m_virtual_screen.top,
		m_virtual_screen.Width(), m_virtual_screen.Height(),
		parent ? parent->GetSafeHwnd() : NULL,
		NULL,
		NULL);

	if (!ok)
		return false;

	::SetWindowDisplayAffinity(m_hWnd, WDA_EXCLUDEFROMCAPTURE);

	if (!capture_virtual_screen_to_d2())
	{
		DestroyWindow();
		return false;
	}

	ShowWindow(SW_SHOW);
	SetForegroundWindow();
	SetCapture();
	return true;
}

bool CSCFrozenOverlayDlg::capture_virtual_screen_to_d2()
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
		//BitBlt 결과는 BGRX (alpha=0). PBGRA 로 업로드 / 폴백 클립보드 양쪽 모두 0xFF 필요.
		BYTE* p = static_cast<BYTE*>(bits);
		const int total = w * h;
		for (int i = 0; i < total; ++i)
			p[i * 4 + 3] = 0xFF;

		hr = m_d2.init(m_hWnd, w, h);
		if (SUCCEEDED(hr))
			hr = m_frozen.load(m_d2.get_WICFactory(), m_d2.get_d2dc(), bits, w, h, 4);
	}

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

void CSCFrozenOverlayDlg::run_modal_loop(CWnd* /*caller_for_quit_propagation*/)
{
	MSG msg = {};
	bool quit_posted = false;
	while (GetSafeHwnd() != NULL)
	{
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				quit_posted = true;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (quit_posted)
			break;
		if (GetSafeHwnd() != NULL)
			WaitMessage();
	}
	if (quit_posted)
		PostQuitMessage(static_cast<int>(msg.wParam));
}

void CSCFrozenOverlayDlg::finish()
{
	if (::GetCapture() == m_hWnd)
		::ReleaseCapture();
	if (GetSafeHwnd())
		DestroyWindow();
}

BOOL CSCFrozenOverlayDlg::OnEraseBkgnd(CDC* /*pDC*/)
{
	//전체를 OnPaint 의 D2D 로 그리므로 GDI 지우기 비활성.
	return TRUE;
}

void CSCFrozenOverlayDlg::OnPaint()
{
	CPaintDC dc(this);

	ID2D1DeviceContext* d2dc = m_d2.get_d2dc();
	if (!d2dc)
		return;

	d2dc->BeginDraw();
	d2dc->SetTransform(D2D1::Matrix3x2F::Identity());

	if (m_frozen.is_valid())
		m_frozen.draw(d2dc, 0, 0);

	on_overlay_paint(d2dc);

	HRESULT hr = d2dc->EndDraw();
	if (SUCCEEDED(hr))
		m_d2.get_swapchain()->Present(0, 0);
	else if (hr == D2DERR_RECREATE_TARGET)
		m_d2.handle_device_lost();
}

void CSCFrozenOverlayDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	//[중요] SetCapture 중에는 WM_SETCURSOR 가 발송되지 않으므로 OnSetCursor 가 호출 안 됨.
	//WM_MOUSEMOVE 는 캡처 중에도 정상 발송되므로 여기서 매번 SetCursor 강제 → 커서가 항상 의도대로.
	HCURSOR hc = query_cursor(point);
	if (!hc)
		hc = ::LoadCursor(NULL, IDC_CROSS);	//derived 가 query_cursor 를 override 안 했으면 십자
	::SetCursor(hc);

	on_mouse_move(nFlags, point);
}

void CSCFrozenOverlayDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	on_mouse_down(nFlags, point);
}

void CSCFrozenOverlayDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	on_mouse_up(nFlags, point);
}

void CSCFrozenOverlayDlg::OnRButtonDown(UINT /*nFlags*/, CPoint point)
{
	on_right_click(point);
}

void CSCFrozenOverlayDlg::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	//run_modal_loop 가 자체 PeekMessage/DispatchMessage 를 돌려서 PreTranslateMessage 가
	//호출되지 않으므로 ESC/Enter 등은 메시지 맵 (ON_WM_KEYDOWN) 으로 받아야 한다.
	if (on_key_down(nChar))
		return;
	CDialog::OnKeyDown(nChar, nRepCnt, nFlags);
}

BOOL CSCFrozenOverlayDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	HCURSOR hc = query_cursor(CPoint(pt));
	if (hc)
	{
		::SetCursor(hc);
		return TRUE;
	}
	return CDialog::OnSetCursor(pWnd, nHitTest, message);
}

BOOL CSCFrozenOverlayDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
	{
		if (on_key_down(static_cast<UINT>(pMsg->wParam)))
			return TRUE;
	}
	return CDialog::PreTranslateMessage(pMsg);
}
