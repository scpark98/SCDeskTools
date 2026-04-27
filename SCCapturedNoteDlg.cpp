// SCCapturedNoteDlg.cpp

#include "pch.h"
#include "SCCapturedNoteDlg.h"

// ------------------- CSCCapturedNoteDlg ------------------------------------

IMPLEMENT_DYNAMIC(CSCCapturedNoteDlg, CDialog)

BEGIN_MESSAGE_MAP(CSCCapturedNoteDlg, CDialog)
	ON_WM_SIZE()
	ON_WM_NCHITTEST()
	ON_WM_NCCALCSIZE()
	ON_WM_CONTEXTMENU()
	ON_WM_NCRBUTTONUP()
	ON_WM_NCACTIVATE()
	ON_WM_NCMOUSEMOVE()
	ON_REGISTERED_MESSAGE(Message_CGdiButton, &CSCCapturedNoteDlg::on_message_CGdiButton)
	ON_BN_CLICKED(kIdBtnClose, &CSCCapturedNoteDlg::OnBnClickedCloseButton)
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

	//WS_EX_LAYERED 활성화 ? Ctrl+wheel 로 창 투명도 조절 가능하게.
	ModifyStyleEx(0, WS_EX_LAYERED);
	SetLayeredWindowAttributes(0, m_alpha, LWA_ALPHA);

	//컨테이너 자체 D2D 컨텍스트.
	HRESULT hr = m_d2.init(m_hWnd, win_cx, win_cy);
	if (FAILED(hr))
	{
		DestroyWindow();
		return false;
	}

	//픽셀 데이터를 CSCD2Image 로 업로드. const_cast 는 load() 시그니처 때문 ? 내부에서 read-only 사용.
	hr = m_image.load(m_d2.get_WICFactory(), m_d2.get_d2dc(),
		const_cast<BYTE*>(bgra), w, h, 4);
	if (FAILED(hr) || !m_image.is_valid())
	{
		DestroyWindow();
		return false;
	}

	//자식 이미지 다이얼로그 ? simple_mode 로 줌/팬 만 자동 처리.
	//set_shared_d2dc 는 create() 호출 전에 설정해야 효과 있음.
	m_img_dlg.set_simple_mode(true);	//모든 기능 off
	m_img_dlg.set_enable_pan(true);		//pan 만 enable (Shift+drag 로 활성화 ? note dlg 의 OnNcHitTest 가 routing)
	m_img_dlg.set_shared_d2dc(&m_d2);

	CRect rc_client;
	GetClientRect(rc_client);
	m_img_dlg.create(this, 0, 0, rc_client.Width(), rc_client.Height());
	m_img_dlg.set_image(&m_image);

	//post-paint 콜백 ? m_img_dlg 의 D2D 같은 frame 에 추가 오버레이 그림 (안티앨리어싱, z-order 충돌 없음).
	//본문은 멤버 함수로 분리. 람다는 this 캡처하여 멤버 호출만 위임.
	m_img_dlg.set_post_paint_callback(
		[this](ID2D1DeviceContext* d2dc) { on_img_dlg_post_paint(d2dc); });

	m_initialized = true;

	//BS_OWNERDRAW 는 CGdiButton 이 PreSubclassWindow 에서 자체 추가. 외부 명시 불필요.
	//(CGdiButton::create / PreSubclassWindow 가 외부 BS_OWNERDRAW 를 자동 정규화하지만 안 주는 게 정석.)
	m_button_close.create(_T(""), WS_CHILD, CRect(0, 0, 21, 21), this, kIdBtnClose);
	m_button_close.ShowWindow(SW_SHOW);

	//YouTube/Chrome 닫기 버튼 톤의 빨강 배경 + 흰 X.
	CSCGdiplusBitmap close_btn_bmp(21, 21, Gdiplus::Color(255, 232, 17, 35));
	{
		Gdiplus::Graphics g(close_btn_bmp);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		Gdiplus::Pen pen(Gdiplus::Color(255, 255, 255, 255), 2.0f);
		pen.SetStartCap(Gdiplus::LineCapRound);
		pen.SetEndCap(Gdiplus::LineCapRound);
		const int pad = 5;
		g.DrawLine(&pen, pad,      pad,      21 - pad, 21 - pad);
		g.DrawLine(&pen, 21 - pad, pad,      pad,      21 - pad);
	}
	m_button_close.add_image(&close_btn_bmp);

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

		CRect rbutton = make_rect(cx - m_button_close.width() - 3, 3, m_button_close.width(), m_button_close.height());
		m_button_close.MoveWindow(rbutton);
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
	menu.AppendMenu(MF_STRING, kCmdCopy,    _T("클립보드로 복사(&C)\tCtrl+C"));
	menu.AppendMenu(MF_STRING, kCmdSave,    _T("이미지 저장(&S)...\tCtrl+S"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(flag_100,  kCmdZoom100, _T("100% 크기\tCtrl+W"));
	menu.AppendMenu(flag_fit,  kCmdZoomFit, _T("창에 맞춤(&F)\tCtrl+F"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, kCmdClose,   _T("닫기(&X)\tEsc"));

	SetForegroundWindow();
	int cmd = menu.TrackPopupMenu(
		TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD,
		pt_screen.x, pt_screen.y, this);
	PostMessage(WM_NULL);

	if (cmd > 0)
		execute_cmd(cmd);
}

void CSCCapturedNoteDlg::execute_cmd(int cmd)
{
	switch (cmd)
	{
		case kCmdCopy:
			m_img_dlg.copy_to_clipboard();
			break;
		case kCmdZoom100:
		{
			//100% = 이미지 픽셀 1:1 + 창 크기를 이미지 크기에 맞춰 자동 조정 (화면 80% 안 비율 유지).
			m_img_dlg.fit2ctrl(false);
			m_img_dlg.zoom(0);	//SCD2ImageDlg 컨벤션: zoom(int 0) = 원본 100% (ASee OnMenuZoomOrigin 과 동일)

			int target_cx = m_img_w;
			int target_cy = m_img_h;

			const int cx_screen = ::GetSystemMetrics(SM_CXSCREEN);
			const int cy_screen = ::GetSystemMetrics(SM_CYSCREEN);
			const int max_cx = cx_screen * 80 / 100;
			const int max_cy = cy_screen * 80 / 100;

			if (target_cx > max_cx)
			{
				target_cy = int(double(target_cy) * max_cx / target_cx);
				target_cx = max_cx;
			}
			if (target_cy > max_cy)
			{
				target_cx = int(double(target_cx) * max_cy / target_cy);
				target_cy = max_cy;
			}
			if (target_cx < 80) target_cx = 80;
			if (target_cy < 60) target_cy = 60;

			//NC 오프셋 측정 ? client 가 정확히 target 이 되도록 window 크기 결정.
			CRect rc_window, rc_client;
			GetWindowRect(rc_window);
			GetClientRect(rc_client);
			const int nc_cx = rc_window.Width()  - rc_client.Width();
			const int nc_cy = rc_window.Height() - rc_client.Height();

			SetWindowPos(NULL, 0, 0,
				target_cx + nc_cx, target_cy + nc_cy,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			break;
		}
		case kCmdZoomFit:
			//창에 맞춤 = fit2ctrl(true). zoom() 과 별개의 모드 (m_fit2ctrl flag).
			m_img_dlg.fit2ctrl(true);
			break;
		case kCmdClose:
			DestroyWindow();
			break;

		case kCmdSave:
		{
			//파일 대화상자의 filter 드롭다운에서 포맷 선택. 새 포맷 추가는 filter 문자열만 확장하면 됨.
			LPCTSTR filter =
				_T("PNG 이미지 (*.png)|*.png|")
				_T("JPEG 이미지 (*.jpg;*.jpeg)|*.jpg;*.jpeg|")
				_T("모든 파일 (*.*)|*.*||");

			SYSTEMTIME st;
			::GetLocalTime(&st);

			CString recent_folder = AfxGetApp()->GetProfileString(_T("settings"), _T("recent_folder"), get_exe_directory());
			CString default_name;
			default_name.Format(_T("%s\\sccapture_%04d%02d%02d_%02d%02d%02d.png"),
				recent_folder,
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

			CFileDialog dlg(FALSE, _T("png"), default_name,
				OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, filter, this);

			if (dlg.DoModal() != IDOK)
				break;

			CString path = dlg.GetPathName();
			CString ext = ::PathFindExtension(path);
			ext.MakeLower();
			const bool is_jpg = (ext == _T(".jpg") || ext == _T(".jpeg"));

			const float quality = is_jpg ? 0.92f : 1.0f;	//PNG 는 무손실이라 quality 의미 없음
			HRESULT hr = m_img_dlg.save(path, quality);
			if (FAILED(hr))
				AfxMessageBox(_T("이미지 저장 실패."));

			AfxGetApp()->WriteProfileString(_T("settings"), _T("recent_folder"), get_part(path, fn_folder));
			break;
		}
	}
}

BOOL CSCCapturedNoteDlg::PreTranslateMessage(MSG* pMsg)
{
	//자식 (CSCD2ImageDlg simple_mode) 이 마우스/키를 거의 처리하지 않으므로
	//PreTranslateMessage 단계에서 부모가 가로채서 모두 처리한다.

	if (pMsg->message == WM_MOUSEMOVE)
	{
		TRACE(_T("PreTranslateMessage, WM_MOUSEMOVE\n"));
	}
	/*
		//마우스 이동 시 닫기 버튼 노출 제어. 이미지 가장자리 근처로 마우스가 오면 버튼이 나타나고 멀어지면 사라진다.
		//버튼이 마우스에 가려지는 경우가 있는데, 이 경우는 버튼이 항상 보이는 게 낫다고 판단하여 노출 유지.
		POINT pt = { LOWORD(pMsg->lParam), HIWORD(pMsg->lParam) };
		ClientToScreen(&pt);
		CRect rc;
		GetWindowRect(rc);
		if (m_button_close.GetSafeHwnd())
		{
			if (m_button_close.IsWindowVisible() == false && pt.x > rc.right - 32 && pt.y < rc.top + 32)
				m_button_close.ShowWindow(SW_SHOW);
			else if (m_button_close.IsWindowVisible() && (pt.x <= rc.right - 32 || pt.y >= rc.top + 32))
				m_button_close.ShowWindow(SW_HIDE);
		}
	}
	//휠 = 줌. SCD2ImageDlg simple_mode 는 자체 휠 처리 X.
	//커서 아래의 이미지 픽셀이 줌 후에도 같은 화면 위치에 머무르도록 offset 보정.
	//   zoom_after = zoom_before * ratio
	//   원하는 화면 위치 cur 가 그대로 유지되려면:
	//   dx = (cur.x - rc_before.left) * (1 - ratio)   (sy 도 동일)
	*/
	else if (pMsg->message == WM_MOUSEWHEEL)
	{
		short zDelta = static_cast<short>(HIWORD(pMsg->wParam));

		//Ctrl+wheel = 창 투명도 조정. wParam 의 LOWORD 에 MK_CONTROL 비트 들어옴.
		const bool ctrl = (LOWORD(pMsg->wParam) & MK_CONTROL) != 0;
		if (ctrl)
		{
			const int step = 16;
			int alpha = m_alpha + (zDelta > 0 ? step : -step);
			if (alpha > 255) alpha = 255;
			if (alpha < 64)  alpha = 64;	//완전 투명 방지 (창을 다시 못 찾는 사고 방지)
			m_alpha = static_cast<BYTE>(alpha);
			SetLayeredWindowAttributes(0, m_alpha, LWA_ALPHA);
			return TRUE;
		}

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

		//Ctrl 조합 ? 컨텍스트 메뉴 캡션에 표시된 단축키. execute_cmd 로 메뉴와 동일 경로 실행.
		case 'C':	//Ctrl+C = 클립보드 복사
		case 'S':	//Ctrl+S = 이미지 저장
		case 'W':	//Ctrl+W = 100% 크기
		case 'F':	//Ctrl+F = 창에 맞춤
			if (::GetAsyncKeyState(VK_CONTROL) & 0x8000)
			{
				int cmd = 0;
				switch (pMsg->wParam)
				{
				case 'C': cmd = kCmdCopy;    break;
				case 'S': cmd = kCmdSave;    break;
				case 'W': cmd = kCmdZoom100; break;
				case 'F': cmd = kCmdZoomFit; break;
				}
				if (cmd)
				{
					execute_cmd(cmd);
					return TRUE;
				}
			}
			break;
		}
	}
	else if (pMsg->message == WM_SYSKEYDOWN)
	{
		switch (pMsg->wParam)
		{
			case '1':
				m_img_dlg.set_interpolation_mode(D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
				return TRUE;
			case '2':
				m_img_dlg.set_interpolation_mode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
				return TRUE;

			//Alt+Left/Right = 15° 회전, Alt+Shift+Left/Right = 1° 회전.
			case VK_LEFT:
			case VK_RIGHT:
			{
				const bool shift = (::GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
				const double step = shift ? 1.0 : 15.0;
				const double sign = (pMsg->wParam == VK_RIGHT) ? +1.0 : -1.0;
				m_img_dlg.set_render_angle(m_img_dlg.get_render_angle() + sign * step);
				return TRUE;
			}
		}
	}
	//WM_LBUTTONDOWN / WM_MOUSEMOVE / WM_LBUTTONUP 은 m_img_dlg (CSCD2ImageDlg) 가 자체 처리.
	//enable_pan=true 라 base 가 native pan 처리. 비-Shift 드래그는 부모의 OnNcHitTest 가
	//HTCAPTION 으로 반환해 Windows modal move 가 처리.

	return CDialog::PreTranslateMessage(pMsg);
}

void CSCCapturedNoteDlg::OnOK()
{
	//modeless 다이얼로그라 base OnOK 의 EndDialog 는 의미 없음. Enter 키는 무시.
}

void CSCCapturedNoteDlg::OnCancel()
{
	//Esc 가 PreTranslate 에서 미처 잡히지 않은 경우의 fallback. close button 도 이 경로 사용.
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

void CSCCapturedNoteDlg::OnBnClickedCloseButton()
{
	//PostMessage 인 이유: CGdiButton::OnLButtonUp 이 BN_CLICKED 발송 후 (line 2011) 추가로
	//CGdiButtonMessage 발송 (line 2013) + parent->m_hWnd 등 접근. 여기서 동기 DestroyWindow 시
	//note dlg 가 즉시 self-delete → CGdiButton 의 잔여 코드가 freed parent 접근 → use-after-free.
	//PostMessage 로 destroy 를 다음 메시지 사이클로 미룸 → CGdiButton 의 OnLButtonUp 안전하게 종료.
	PostMessage(WM_COMMAND, IDCANCEL);
}

void CSCCapturedNoteDlg::on_img_dlg_post_paint(ID2D1DeviceContext* d2dc)
{
	return;

	//m_img_dlg 의 OnPaint 가 D2D BeginDraw / EndDraw 사이에서 호출 → 같은 frame 에 오버레이.
	//테스트용 빨간 사각형 (좌상단 20,20 부터 100x60).
	if (!d2dc)
		return;

//	if (m_pt_mouse)
	ComPtr<ID2D1SolidColorBrush> br_red;
	d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red, 1.0f), br_red.GetAddressOf());
	if (!br_red)
		return;

	D2D1_RECT_F r = D2D1::RectF(20.0f, 20.0f, 120.0f, 80.0f);
	d2dc->DrawRectangle(r, br_red.Get(), 3.0f);
}

void CSCCapturedNoteDlg::OnNcMouseMove(UINT nHitTest, CPoint point)
{
	//point 는 screen 좌표. OnNcHitTest 가 client 영역에 대해 HTCAPTION 을 리턴하므로
	//client 위 마우스 이동도 모두 여기로 옴 (resize 보더의 HTTOP/HTRIGHT 등도 포함).
	TRACE(_T("nc mouse move. hit=%u screen=(%d, %d)\n"), nHitTest, point.x, point.y);
	//m_pt_mouse = point;
	CRect rc;
	GetClientRect(rc);
	ScreenToClient(&point);

	//버튼 (20x20 + margin 2 = 우상단 24x24 영역) 위에 마우스가 있을 때만 표시.
	if (point.x > rc.Width() - 24 && point.y < 24)
		m_button_close.ShowWindow(SW_SHOW);
	else
		m_button_close.ShowWindow(SW_HIDE);

	CDialog::OnNcMouseMove(nHitTest, point);
}

LRESULT CSCCapturedNoteDlg::on_message_CGdiButton(WPARAM wParam, LPARAM lParam)
{
	CGdiButtonMessage* msg = (CGdiButtonMessage*)wParam;
	trace(msg->message);
	if (msg->pThis == &m_button_close)
	{
		switch (msg->message)
		{
		case BN_CLICKED:
			DestroyWindow();
			break;
		}
	}

	return 0;
}
