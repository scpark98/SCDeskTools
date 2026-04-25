// SCCapturedNoteDlg.cpp

#include "pch.h"
#include "SCCapturedNoteDlg.h"

// ------------------- CSCNoteImageDlg ---------------------------------------

IMPLEMENT_DYNAMIC(CSCNoteImageDlg, CSCD2ImageDlg)

BEGIN_MESSAGE_MAP(CSCNoteImageDlg, CSCD2ImageDlg)
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
END_MESSAGE_MAP()

void CSCNoteImageDlg::OnLButtonDown(UINT /*nFlags*/, CPoint /*point*/)
{
	//여기 도착하는 클릭 = 부모 OnNcHitTest 가 HTCLIENT 반환한 케이스 = Shift 보유.
	//비Shift 클릭은 부모가 HTCAPTION 으로 반환하여 Windows modal move 가 처리 → 여기 안 옴.
	if (get_fit2ctrl())
		fit2ctrl(false);

	::GetCursorPos(&m_pan_last);
	m_panning = true;
	SetCapture();
}

void CSCNoteImageDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_panning && (nFlags & MK_LBUTTON))
	{
		POINT cur;
		::GetCursorPos(&cur);
		const int dx = cur.x - m_pan_last.x;
		const int dy = cur.y - m_pan_last.y;
		if (dx || dy)
		{
			scroll(dx, dy);
			m_pan_last = cur;
		}
		return;
	}
	CSCD2ImageDlg::OnMouseMove(nFlags, point);
}

void CSCNoteImageDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_panning)
	{
		if (::GetCapture() == m_hWnd)
			::ReleaseCapture();
		m_panning = false;
		return;
	}
	CSCD2ImageDlg::OnLButtonUp(nFlags, point);
}


// ------------------- CSCCapturedNoteDlg ------------------------------------

IMPLEMENT_DYNAMIC(CSCCapturedNoteDlg, CDialog)

BEGIN_MESSAGE_MAP(CSCCapturedNoteDlg, CDialog)
	ON_WM_SIZE()
	ON_WM_NCHITTEST()
	ON_WM_NCCALCSIZE()
	ON_WM_CONTEXTMENU()
	ON_WM_NCRBUTTONUP()
	ON_WM_NCACTIVATE()
END_MESSAGE_MAP()

CSCCapturedNoteDlg::CSCCapturedNoteDlg() : CDialog()
{
}

CSCCapturedNoteDlg::~CSCCapturedNoteDlg()
{
}

CSCCapturedNoteDlg* CSCCapturedNoteDlg::spawn(const BYTE* bgra_top_down, int w, int h, const POINT* pos_screen)
{
	auto* p = new CSCCapturedNoteDlg();
	if (!p->init_with_image(bgra_top_down, w, h, pos_screen))
	{
		delete p;
		return nullptr;
	}
	return p;
}

bool CSCCapturedNoteDlg::init_with_image(const BYTE* bgra, int w, int h, const POINT* pos_screen)
{
	if (!bgra || w <= 0 || h <= 0)
		return false;

	m_img_w = w;
	m_img_h = h;

	//캡션 / 보더 없는 popup. 검은 배경 (이미지 영역 외에 잠깐 보이더라도 무난).
	LPCTSTR wnd_class = ::AfxRegisterWndClass(
		0,
		::LoadCursor(NULL, IDC_ARROW),
		reinterpret_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));

	//시작 사이즈 = 이미지 사이즈, 단 화면의 80% 를 넘지 않게 ratio 보존 축소.
	const int cx_screen = ::GetSystemMetrics(SM_CXSCREEN);
	const int cy_screen = ::GetSystemMetrics(SM_CYSCREEN);
	const int max_cx = cx_screen * 80 / 100;
	const int max_cy = cy_screen * 80 / 100;

	int win_cx = w;
	int win_cy = h;
	if (win_cx > max_cx)
	{
		win_cy = static_cast<int>(static_cast<double>(win_cy) * max_cx / win_cx);
		win_cx = max_cx;
	}
	if (win_cy > max_cy)
	{
		win_cx = static_cast<int>(static_cast<double>(win_cx) * max_cy / win_cy);
		win_cy = max_cy;
	}
	if (win_cx < 80) win_cx = 80;
	if (win_cy < 60) win_cy = 60;

	int x, y;
	if (pos_screen)
	{
		x = pos_screen->x;
		y = pos_screen->y;
	}
	else
	{
		x = (cx_screen - win_cx) / 2;
		y = (cy_screen - win_cy) / 2;
	}

	//owner = main dialog. owned popup 으로 만들어 메인 다이얼로그 destroy 시 자동으로
	//PostNcDestroy → delete this 가 발화되어 메모리 leak 방지.
	//child window 가 아니므로 자유 이동/리사이즈/별도 z-order 동작은 그대로.
	HWND hOwner = NULL;
	if (CWnd* pMain = AfxGetMainWnd())
		hOwner = pMain->GetSafeHwnd();

	//WS_EX_TOOLWINDOW: taskbar/Alt+Tab 비노출.
	//WS_EX_TOPMOST: 포스트잇처럼 항상 다른 창 위. 캡션바가 없어 한번 가려지면 다시 띄울
	//방법이 없으므로 topmost 가 사실상 필수 (사용자 요구).
	BOOL ok = CreateEx(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
		wnd_class,
		_T("SCDeskTools Note"),
		WS_POPUP | WS_SIZEBOX,
		x, y, win_cx, win_cy,
		hOwner,
		NULL,
		NULL);

	if (!ok)
		return false;

	//컨테이너 자체 D2D 컨텍스트.
	HRESULT hr = m_d2.init(m_hWnd, win_cx, win_cy);
	if (FAILED(hr))
	{
		DestroyWindow();
		return false;
	}

	//픽셀 데이터를 CSCD2Image 로 업로드. const_cast 는 load() 시그니처 때문 — 내부에서 read-only 사용.
	hr = m_image.load(m_d2.get_WICFactory(), m_d2.get_d2dc(),
		const_cast<BYTE*>(bgra), w, h, 4);
	if (FAILED(hr) || !m_image.is_valid())
	{
		DestroyWindow();
		return false;
	}

	//자식 이미지 다이얼로그 — simple_mode 로 줌/팬 만 자동 처리.
	//set_shared_d2dc 는 create() 호출 전에 설정해야 효과 있음.
	m_img_dlg.set_simple_mode(true);
	m_img_dlg.set_shared_d2dc(&m_d2);

	CRect rc_client;
	GetClientRect(rc_client);
	m_img_dlg.create(this, 0, 0, rc_client.Width(), rc_client.Height());
	m_img_dlg.set_image(&m_image);

	m_initialized = true;

	ShowWindow(SW_SHOW);
	SetForegroundWindow();
	return true;
}

void CSCCapturedNoteDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);

	if (m_initialized && m_img_dlg.GetSafeHwnd())
	{
		//클라이언트 전체에 자식을 fit. 자식이 알아서 fit-to-ctrl 또는 zoom 모드에 맞춰 다시 그림.
		m_img_dlg.MoveWindow(0, 0, cx, cy, TRUE);
	}
}

LRESULT CSCCapturedNoteDlg::OnNcHitTest(CPoint point)
{
	//point 는 screen 좌표.
	CRect rc;
	GetWindowRect(rc);

	//(1) 가장자리 8px → 8방향 resize 핸들.
	const int E = static_cast<int>(kEdgeResize);
	const bool L = (point.x <  rc.left  + E);
	const bool R = (point.x >= rc.right - E);
	const bool T = (point.y <  rc.top   + E);
	const bool B = (point.y >= rc.bottom - E);

	if (T && L) return HTTOPLEFT;
	if (T && R) return HTTOPRIGHT;
	if (B && L) return HTBOTTOMLEFT;
	if (B && R) return HTBOTTOMRIGHT;
	if (L) return HTLEFT;
	if (R) return HTRIGHT;
	if (T) return HTTOP;
	if (B) return HTBOTTOM;

	//(2) 클라이언트 영역.
	//Shift 누른 상태면 HTCLIENT 로 자식 (m_img_dlg) 이 받아 pan.
	//그 외에는 HTCAPTION 반환 → Windows DefWindowProc 가 modal move loop 자동 처리.
	//HTCAPTION 분기는 ASee 가 검증한 가장 신뢰성 높은 창 이동 경로.
	if (::GetAsyncKeyState(VK_SHIFT) & 0x8000)
		return HTCLIENT;

	return HTCAPTION;
}

void CSCCapturedNoteDlg::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
{
	//캡션바 없는 popup 에서 default 가 남기는 상단 흰색 NC 영역을 client 로 흡수.
	//client rect 의 top 을 6px 위로 확장하여 그 영역을 우리 클라이언트가 덮게 한다.
	//CASeeDlg::OnNcCalcSize 와 동일 패턴.

	if (bCalcValidRects && lpncsp)
		lpncsp->rgrc[0].top -= 6;

	CDialog::OnNcCalcSize(bCalcValidRects, lpncsp);
}

void CSCCapturedNoteDlg::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	//point == (-1,-1) 이면 키보드 메뉴 키. 그 외엔 마우스 위치 (screen coord).
	if (point.x == -1 && point.y == -1)
	{
		CRect rc;
		GetWindowRect(rc);
		point.x = rc.left + rc.Width() / 2;
		point.y = rc.top  + rc.Height() / 2;
	}
	show_context_menu(point);
}

void CSCCapturedNoteDlg::OnNcRButtonUp(UINT /*nHitTest*/, CPoint point)
{
	//OnNcHitTest 가 클라이언트 영역을 HTCAPTION 으로 반환하므로 거기서 우클릭은
	//WM_CONTEXTMENU 가 아닌 WM_NCRBUTTONUP 으로 들어온다. 컨텍스트 메뉴를 직접 띄움.
	//point 는 NC 메시지 표준대로 screen 좌표.
	show_context_menu(point);
}

void CSCCapturedNoteDlg::show_context_menu(CPoint pt_screen)
{
	const bool is_fit  = m_img_dlg.get_fit2ctrl();
	const double zoom  = m_img_dlg.get_zoom_ratio();
	const bool is_100  = (!is_fit && zoom > 0.99 && zoom < 1.01);

	const UINT flag_100 = MF_STRING | (is_100 ? MF_CHECKED : MF_UNCHECKED);
	const UINT flag_fit = MF_STRING | (is_fit ? MF_CHECKED : MF_UNCHECKED);

	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, kCmdCopy,    _T("클립보드로 복사(&C)"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(flag_100,  kCmdZoom100, _T("100% 크기"));
	menu.AppendMenu(flag_fit,  kCmdZoomFit, _T("창에 맞춤(&F)"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, kCmdClose,   _T("닫기(&X)"));

	SetForegroundWindow();
	int cmd = menu.TrackPopupMenu(
		TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
		pt_screen.x, pt_screen.y, this);
	PostMessage(WM_NULL);

	switch (cmd)
	{
	case kCmdCopy:
		m_img_dlg.copy_to_clipboard();
		break;
	case kCmdZoom100:
		//SCD2ImageDlg 컨벤션: zoom(int 0) = 원본 100% (ASee OnMenuZoomOrigin 과 동일).
		m_img_dlg.zoom(0);
		break;
	case kCmdZoomFit:
		//창에 맞춤 = fit2ctrl(true). zoom() 과 별개의 모드 (m_fit2ctrl flag).
		m_img_dlg.fit2ctrl(true);
		break;
	case kCmdClose:
		DestroyWindow();
		break;
	}
}

BOOL CSCCapturedNoteDlg::PreTranslateMessage(MSG* pMsg)
{
	//자식 (CSCD2ImageDlg simple_mode) 이 마우스/키를 거의 처리하지 않으므로
	//PreTranslateMessage 단계에서 부모가 가로채서 모두 처리한다.

	//휠 = 줌. SCD2ImageDlg simple_mode 는 자체 휠 처리 X.
	//커서 아래의 이미지 픽셀이 줌 후에도 같은 화면 위치에 머무르도록 offset 보정.
	//   zoom_after = zoom_before * ratio
	//   원하는 화면 위치 cur 가 그대로 유지되려면:
	//   dx = (cur.x - rc_before.left) * (1 - ratio)   (sy 도 동일)
	if (pMsg->message == WM_MOUSEWHEEL)
	{
		short zDelta = static_cast<short>(HIWORD(pMsg->wParam));

		POINT cur_screen;
		::GetCursorPos(&cur_screen);
		CPoint cur_client(cur_screen);
		m_img_dlg.ScreenToClient(&cur_client);

		CRect rc_before = m_img_dlg.get_displayed_rect();
		const double zoom_before = m_img_dlg.get_zoom_ratio();

		m_img_dlg.zoom(zDelta > 0 ? 1 : -1);

		const double zoom_after = m_img_dlg.get_zoom_ratio();

		if (zoom_before > 0.0 && zoom_after != zoom_before)
		{
			const double ratio = zoom_after / zoom_before;
			const double dx = (cur_client.x - rc_before.left) * (1.0 - ratio);
			const double dy = (cur_client.y - rc_before.top)  * (1.0 - ratio);
			if (dx != 0.0 || dy != 0.0)
				m_img_dlg.scroll(static_cast<int>(dx), static_cast<int>(dy));
		}
		return TRUE;
	}

	if (pMsg->message == WM_KEYDOWN)
	{
		switch (pMsg->wParam)
		{
		case VK_ESCAPE:
			DestroyWindow();
			return TRUE;

		case VK_ADD:		//숫자패드 +
		case VK_OEM_PLUS:	//일반 키보드 = / + (Shift 시 +)
			m_img_dlg.zoom(1);
			return TRUE;

		case VK_SUBTRACT:	//숫자패드 -
		case VK_OEM_MINUS:	//일반 키보드 -
			m_img_dlg.zoom(-1);
			return TRUE;

		case '0':			//일반 키보드 0
		case VK_NUMPAD0:	//숫자패드 0
			m_img_dlg.zoom(0);	//원본 100% (ASee 컨벤션)
			return TRUE;
		}
	}

	//WM_LBUTTONDOWN / WM_MOUSEMOVE / WM_LBUTTONUP 은 자식 (CSCNoteImageDlg) 의
	//OnLButton*/OnMouseMove override 가 직접 처리하므로 여기서는 다루지 않는다.
	//PreTranslate walk-up 에 의존하지 않아 라우팅이 안정적.

	return CDialog::PreTranslateMessage(pMsg);
}

void CSCCapturedNoteDlg::OnOK()
{
	//modeless 다이얼로그라 base OnOK 의 EndDialog 는 의미 없음. Enter 키는 무시.
}

void CSCCapturedNoteDlg::OnCancel()
{
	//Esc 가 PreTranslate 에서 미처 잡히지 않은 경우의 fallback.
	DestroyWindow();
}

void CSCCapturedNoteDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	delete this;
}

BOOL CSCCapturedNoteDlg::OnNcActivate(BOOL bActive)
{
	// TODO: 여기에 메시지 처리기 코드를 추가 및/또는 기본값을 호출합니다.
	return TRUE;
	return CDialog::OnNcActivate(bActive);
}
