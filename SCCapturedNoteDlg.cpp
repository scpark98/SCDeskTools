// SCCapturedNoteDlg.cpp

#include "pch.h"
#include "SCCapturedNoteDlg.h"
#include "Common/Functions.h"

//32bpp BGRA top-down 픽셀의 가장자리만 블러로 부드럽게 (in-place).
//premultiplied 도메인에서 BGRA 전체에 separable box blur → un-premultiply 로 straight alpha 복원.
//마지막에 원본 alpha=255 픽셀의 RGB 를 복원 → 내부 화질은 보존, 가장자리만 흐려짐.
//- 깊은 내부 (이웃이 모두 alpha=255) : alpha=255 + 원본 RGB 그대로
//- 내부 가장자리 부근 : alpha 감소 + 원본 RGB 복원 (부드럽게 fade)
//- edge 띠 / 외부 가까운 영역 : 블러된 RGB + 블러된 alpha (안쪽 색이 자연스럽게 새어나와 검은 halo 없음)
//- 반복 호출 시 누적 — edge 띠가 점점 넓고 뿌옇게 확장. 깊은 내부는 흐려지지 않음.
static void apply_edge_blur(BYTE* px, int w, int h, int radius)
{
	if (!px || w <= 0 || h <= 0 || radius <= 0)
		return;

	const int N = w * h;
	const size_t SN = (size_t)N;

	//원본 BGRA 보관 — 블러 후 거리 기반 선형 blend 로 원본/블러 RGB 합성.
	std::vector<BYTE> orig(SN * 4);
	memcpy(orig.data(), px, SN * 4);

	//각 픽셀에서 가장 가까운 비-완전불투명 픽셀까지의 Manhattan 거리 (2-pass chamfer).
	//이 거리로 RGB blend 비율 결정 — 깊은 내부 (dist >= threshold) 는 100% 원본,
	//경계 (dist=1) 는 거의 블러, 비-완전불투명 (dist=0) 는 100% 블러.
	const int INF = w + h + 8;
	std::vector<int> dist(SN, 0);
	for (int i = 0; i < N; ++i)
		dist[i] = (orig[i * 4 + 3] == 255) ? INF : 0;
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			const int idx = y * w + x;
			if (dist[idx] == 0) continue;
			int v = dist[idx];
			if (x > 0) v = min(v, dist[idx - 1] + 1);
			if (y > 0) v = min(v, dist[idx - w] + 1);
			dist[idx] = v;
		}
	}
	for (int y = h - 1; y >= 0; --y)
	{
		for (int x = w - 1; x >= 0; --x)
		{
			const int idx = y * w + x;
			if (dist[idx] == 0) continue;
			int v = dist[idx];
			if (x < w - 1) v = min(v, dist[idx + 1] + 1);
			if (y < h - 1) v = min(v, dist[idx + w] + 1);
			dist[idx] = v;
		}
	}

	std::vector<float> b(SN), g(SN), r(SN), a(SN);

	//premultiply: 투명 영역의 RGB 기여를 0 으로 정규화.
	for (int i = 0; i < N; ++i)
	{
		const float A = float(px[i * 4 + 3]);
		const float s = A / 255.0f;
		b[i] = float(px[i * 4 + 0]) * s;
		g[i] = float(px[i * 4 + 1]) * s;
		r[i] = float(px[i * 4 + 2]) * s;
		a[i] = A;
	}

	//이미지 영역 밖은 0 으로 취급 (zero padding). 경계 clamp 방식이면 사각형 경계에서
	//샘플이 모두 같은 값이라 블러가 안 일어나 이미지 변과 붙은 가장자리에서 효과 없음.
	//zero padding 시 경계 부근 픽셀의 kernel 합이 자연스럽게 작아져 alpha 가 fade.
	auto blur1d = [&](std::vector<float>& src)
	{
		std::vector<float> tmp(SN);
		const int diameter = 2 * radius + 1;
		const float inv_d = 1.0f / float(diameter);

		//horizontal
		for (int y = 0; y < h; ++y)
		{
			const float* srow = src.data() + size_t(y) * w;
			float* trow = tmp.data() + size_t(y) * w;
			float sum = 0.0f;
			for (int k = -radius; k <= radius; ++k)
				if (k >= 0 && k < w)
					sum += srow[k];
			for (int x = 0; x < w; ++x)
			{
				trow[x] = sum * inv_d;
				const int xr = x - radius;
				const int xa = x + radius + 1;
				if (xr >= 0 && xr < w) sum -= srow[xr];
				if (xa >= 0 && xa < w) sum += srow[xa];
			}
		}

		//vertical
		for (int x = 0; x < w; ++x)
		{
			float sum = 0.0f;
			for (int k = -radius; k <= radius; ++k)
				if (k >= 0 && k < h)
					sum += tmp[size_t(k) * w + x];
			for (int y = 0; y < h; ++y)
			{
				src[size_t(y) * w + x] = sum * inv_d;
				const int yr = y - radius;
				const int ya = y + radius + 1;
				if (yr >= 0 && yr < h) sum -= tmp[size_t(yr) * w + x];
				if (ya >= 0 && ya < h) sum += tmp[size_t(ya) * w + x];
			}
		}
	};

	blur1d(b);
	blur1d(g);
	blur1d(r);
	blur1d(a);

	//un-premultiply: straight alpha 로 복원해 WIC 가 (BGRA straight) 로 인식하도록.
	for (int i = 0; i < N; ++i)
	{
		const float A = a[i];
		if (A < 0.5f)
		{
			px[i * 4 + 0] = 0;
			px[i * 4 + 1] = 0;
			px[i * 4 + 2] = 0;
			px[i * 4 + 3] = 0;
			continue;
		}
		const float s = 255.0f / A;
		auto clamp255 = [](float v) -> BYTE
		{
			if (v < 0.0f) return 0;
			if (v > 255.0f) return 255;
			return BYTE(v + 0.5f);
		};

		//거리 기반 선형 blend: 깊은 내부(dist >= threshold) → 원본 RGB,
		//경계(dist=1) → 거의 블러, 비-완전불투명(dist=0) → 100% 블러.
		//threshold = 2*radius 이면 블러가 도달하는 구간 전체에서 부드럽게 blend → 경계가 시각적으로 안 보임.
		const int blend_threshold = 2 * radius;
		float t = float(dist[i]) / float(blend_threshold);
		if (t > 1.0f) t = 1.0f;
		if (t < 0.0f) t = 0.0f;
		const float u = 1.0f - t;

		const float blurredB = b[i] * s;
		const float blurredG = g[i] * s;
		const float blurredR = r[i] * s;

		px[i * 4 + 0] = clamp255(blurredB * u + float(orig[i * 4 + 0]) * t);
		px[i * 4 + 1] = clamp255(blurredG * u + float(orig[i * 4 + 1]) * t);
		px[i * 4 + 2] = clamp255(blurredR * u + float(orig[i * 4 + 2]) * t);
		px[i * 4 + 3] = clamp255(A);
	}
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
	ON_WM_NCMOUSEMOVE()
	ON_REGISTERED_MESSAGE(Message_CGdiButton, &CSCCapturedNoteDlg::on_message_CGdiButton)
	ON_BN_CLICKED(id_button_close, &CSCCapturedNoteDlg::OnBnClickedCloseButton)
	ON_WM_MOUSEMOVE()
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

	//후속 효과 (gradient edge 등) 에서 다시 사용하므로 픽셀 보관.
	m_bgra_data.assign(bgra, bgra + size_t(w) * size_t(h) * 4);

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

	//WS_CLIPSIBLINGS 없으면 m_img_dlg 가 D2D frame 그릴 때 형제인 m_button_close 영역까지
	//덮어 그려 버튼이 가려진다 (버튼을 클릭하거나 mouse-enter 시점에만 자체 invalidate 로 다시 보임).
	m_img_dlg.ModifyStyle(0, WS_CLIPSIBLINGS);

	//post-paint 콜백 ? m_img_dlg 의 D2D 같은 frame 에 추가 오버레이 그림 (안티앨리어싱, z-order 충돌 없음).
	//본문은 멤버 함수로 분리. 람다는 this 캡처하여 멤버 호출만 위임.
	m_img_dlg.set_post_paint_callback(
		[this](ID2D1DeviceContext* d2dc) { on_img_dlg_post_paint(d2dc); });

	m_initialized = true;

	//BS_OWNERDRAW 는 CGdiButton 이 PreSubclassWindow 에서 자체 추가. 외부 명시 불필요.
	//(CGdiButton::create / PreSubclassWindow 가 외부 BS_OWNERDRAW 를 자동 정규화하지만 안 주는 게 정석.)
	m_button_close.create(_T(""), WS_CHILD, CRect(0, 0, 21, 21), this, id_button_close);
	m_button_close.ShowWindow(SW_SHOW);
	//z-order 최상단으로 — 형제 m_img_dlg 위에 항상 그려지도록 강제.
	m_button_close.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	//닫기 버튼 생성 및 설정.
	CSize sz_button(19, 19);
	CSCGdiplusBitmap close_btn_bmp(sz_button.cx, sz_button.cy, Gdiplus::Color(255, 232, 17, 35));
	{
		Gdiplus::Graphics g(close_btn_bmp);
		const int half = MIN(sz_button.cx, sz_button.cy) / 4;
		const CPoint cp(sz_button.cx / 2, sz_button.cy / 2);
		draw_line(g, cp.x - half, cp.y - half, cp.x + half, cp.y + half,
			Gdiplus::Color::White, 2.0f, Gdiplus::DashStyleSolid, R2_COPYPEN, Gdiplus::LineCapRound);
		draw_line(g, cp.x + half, cp.y - half, cp.x - half, cp.y + half,
			Gdiplus::Color::White, 2.0f, Gdiplus::DashStyleSolid, R2_COPYPEN, Gdiplus::LineCapRound);
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
	const int E = static_cast<int>(edge_resize);
	const bool L = (point.x <	rc.left	+ E);
	const bool R = (point.x >= rc.right - E);
	const bool T = (point.y <	rc.top	+ E);
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
		point.y = rc.top	+ rc.Height() / 2;
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
	const bool is_fit	= m_img_dlg.get_fit2ctrl();
	const double zoom	= m_img_dlg.get_zoom_ratio();
	const bool is_100	= (!is_fit && zoom > 0.99 && zoom < 1.01);

	const UINT flag_100 = MF_STRING | (is_100 ? MF_CHECKED : MF_UNCHECKED);
	const UINT flag_fit = MF_STRING | (is_fit ? MF_CHECKED : MF_UNCHECKED);

	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, cmd_copy,	_T("클립보드로 복사(&C)\tCtrl+C"));
	menu.AppendMenu(MF_STRING, cmd_save,	_T("이미지 저장(&S)...\tCtrl+S"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(flag_100,	cmd_zoom_100, _T("100% 크기\tCtrl+W"));
	menu.AppendMenu(flag_fit,	cmd_zoom_fit, _T("창에 맞춤(&F)\tCtrl+F"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, cmd_gradient_edge, _T("Gradient Edge(&G)"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, cmd_close,	_T("닫기(&X)\tEsc"));

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
		case cmd_copy:
			m_img_dlg.copy_to_clipboard();
			break;
		case cmd_zoom_100:
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
			const int nc_cx = rc_window.Width()	- rc_client.Width();
			const int nc_cy = rc_window.Height() - rc_client.Height();

			SetWindowPos(NULL, 0, 0,
				target_cx + nc_cx, target_cy + nc_cy,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			break;
		}
		case cmd_zoom_fit:
			//창에 맞춤 = fit2ctrl(true). zoom() 과 별개의 모드 (m_fit2ctrl flag).
			m_img_dlg.fit2ctrl(true);
			break;
		case cmd_close:
			DestroyWindow();
			break;

		case cmd_gradient_edge:
		{
			//현재 BGRA 버퍼에 edge 블러 적용 후 D2D 비트맵 다시 로드.
			//첫 호출 시 캔버스를 32px 씩 확장 (alpha=0 마진) → blur 가 자연스럽게 마진 안으로 fade.
			//이후 호출은 확장된 버퍼 그대로 사용해 누적 블러만 진행.
			if (m_bgra_data.empty() || m_img_w <= 0 || m_img_h <= 0)
				break;

			bool resize_pending = false;
			int  pending_x = 0, pending_y = 0, pending_w = 0, pending_h = 0;

			if (!m_edge_padded)
			{
				const int P = 32;

				//패딩으로 인해 시각적으로 이미지가 축소되지 않도록 창 크기를 늘려준다.
				//현재 displayed_rect 의 이미지 1픽셀 당 화면 픽셀 비율 (scale) 을 그대로 유지하도록
				//창 client 크기를 새로운 캔버스 크기 * scale 로 강제 → fit2ctrl 든 zoom 모드든
				//원본 영역 픽셀이 동일한 화면 크기로 그려진다.
				CRect rc_disp = m_img_dlg.get_displayed_rect();
				double sx = (m_img_w  > 0 && rc_disp.Width()  > 0) ? double(rc_disp.Width())  / double(m_img_w)  : 1.0;
				double sy = (m_img_h > 0 && rc_disp.Height() > 0) ? double(rc_disp.Height()) / double(m_img_h) : 1.0;

				const int new_w = m_img_w + 2 * P;
				const int new_h = m_img_h + 2 * P;
				std::vector<BYTE> padded(size_t(new_w) * size_t(new_h) * 4, 0);
				for (int y = 0; y < m_img_h; ++y)
				{
					memcpy(padded.data() + (size_t(y + P) * new_w + P) * 4,
						m_bgra_data.data() + size_t(y) * m_img_w * 4,
						size_t(m_img_w) * 4);
				}
				m_bgra_data = std::move(padded);
				m_img_w = new_w;
				m_img_h = new_h;
				m_edge_padded = true;

				//창 크기 변경은 새 이미지를 로드 후로 미룸 — fit2ctrl 모드에서
				//구 m_image 가 새 창 크기로 stretch 되는 1프레임 잔상을 방지.
				CRect rc_window, rc_client;
				GetWindowRect(rc_window);
				GetClientRect(rc_client);
				const int nc_cx = rc_window.Width()  - rc_client.Width();
				const int nc_cy = rc_window.Height() - rc_client.Height();

				const int new_client_cx = int(double(new_w) * sx + 0.5);
				const int new_client_cy = int(double(new_h) * sy + 0.5);
				const int dx = int(double(P) * sx + 0.5);
				const int dy = int(double(P) * sy + 0.5);

				pending_x = rc_window.left - dx;
				pending_y = rc_window.top  - dy;
				pending_w = new_client_cx + nc_cx;
				pending_h = new_client_cy + nc_cy;
				resize_pending = true;
			}

			const int radius = 4;
			apply_edge_blur(m_bgra_data.data(), m_img_w, m_img_h, radius);

			HRESULT hr = m_image.load(m_d2.get_WICFactory(), m_d2.get_d2dc(),
				m_bgra_data.data(), m_img_w, m_img_h, 4);

			//창 리사이즈 동안 자식 m_img_dlg 가 먼저 paint 되어 stretch 보이는 것을 막기 위해
			//SetRedraw(FALSE) → 새 이미지로 set_image → SetWindowPos → SetRedraw(TRUE) + Invalidate.
			if (resize_pending)
			{
				m_img_dlg.SetRedraw(FALSE);

				if (SUCCEEDED(hr) && m_image.is_valid())
					m_img_dlg.set_image(&m_image);

				SetWindowPos(NULL,
					pending_x, pending_y, pending_w, pending_h,
					SWP_NOZORDER | SWP_NOACTIVATE);

				m_img_dlg.SetRedraw(TRUE);
				m_img_dlg.Invalidate(FALSE);
			}
			else
			{
				if (SUCCEEDED(hr) && m_image.is_valid())
				{
					m_img_dlg.set_image(&m_image);
					m_img_dlg.Invalidate(FALSE);
				}
			}
			break;
		}

		case cmd_save:
		{
			//파일 대화상자의 filter 드롭다운에서 포맷 선택. 새 포맷 추가는 filter 문자열만 확장하면 됨.
			LPCTSTR filter =
				_T("JPEG 이미지 (*.jpg;*.jpeg)|*.jpg;*.jpeg|")
				_T("PNG 이미지 (*.png)|*.png|")
				_T("모든 파일 (*.*)|*.*||");

			SYSTEMTIME st;
			::GetLocalTime(&st);

			CString recent_folder = AfxGetApp()->GetProfileString(_T("settings"), _T("recent_folder"), get_exe_directory());
			CString default_name;
			default_name.Format(_T("%s\\sccapture_%04d%02d%02d_%02d%02d%02d.jpg"),
				recent_folder,
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

			CFileDialog dlg(FALSE, _T("jpg"), default_name,
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
		//여기에 들어오는 경우는 우측 상단에 표시된 닫기버튼에서 마우스가 움직일 때 뿐이다.
		//dlg에서 마우스를 움직이면 HTCAPTION으로 매핑하므로 NcMouseMove가 발생하고
		//shift를 누르면 CSCCapturedNoteDlg::OnMouseMove() 함수가 호출된다.
		TRACE(_T("PreTranslateMessage, WM_MOUSEMOVE\n"));
	}
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
			if (alpha < 64)	alpha = 64;	//완전 투명 방지 (창을 다시 못 찾는 사고 방지)
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
			const double dy = (cur_client.y - rc_before.top)	* (1.0 - ratio);
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
				case 'C': cmd = cmd_copy;	break;
				case 'S': cmd = cmd_save;	break;
				case 'W': cmd = cmd_zoom_100; break;
				case 'F': cmd = cmd_zoom_fit; break;
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
	if (!d2dc)
		return;

	CRect rc;
	m_img_dlg.GetClientRect(&rc);

	const int margin = 6;

	if (m_img_w > 0 && m_img_h > 0)
	{
		WCHAR size_text[64];
		swprintf_s(size_text, L"(%d x %d) (%.3f:1)", m_img_w, m_img_h, double(m_img_w) / double(m_img_h));
		draw_text(d2dc, CRect(rc.left + margin, rc.top, rc.right, rc.bottom - margin), size_text,
			_T("Segoe UI"), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
			Gdiplus::Color::White, Gdiplus::Color::Black, Gdiplus::Color::Black, Gdiplus::Color::Transparent,
			1.0f, DT_LEFT | DT_BOTTOM);
	}

	if (m_hover_pixel.X < 0.0f || m_hover_pixel.Y < 0.0f)
		return;

	WCHAR text[64];
	swprintf_s(text, L"(%d, %d)", int(m_hover_pixel.X), int(m_hover_pixel.Y));

	draw_text(d2dc, CRect(rc.left, rc.top, rc.right - margin, rc.bottom - margin), text,
		_T("Segoe UI"), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
		Gdiplus::Color::White, Gdiplus::Color::Black, Gdiplus::Color::Black, Gdiplus::Color::Transparent,
		1.0f, DT_RIGHT | DT_BOTTOM);
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

	//호버 픽셀 좌표 갱신 — m_img_dlg client 가 NoteDlg client 와 동일 (0,0 부터 전체 차지).
	if (m_img_w > 0 && m_img_h > 0)
	{
		Gdiplus::PointF prev = m_hover_pixel;

		float ix, iy;
		get_real_coord_from_screen_coord(m_img_dlg.get_displayed_rect(), m_img_w,
			(float)point.x, (float)point.y, &ix, &iy);
		if (ix >= 0.0f && ix < (float)m_img_w && iy >= 0.0f && iy < (float)m_img_h)
			m_hover_pixel = Gdiplus::PointF(ix, iy);
		else
			m_hover_pixel = Gdiplus::PointF(-1.0f, -1.0f);

		if (prev.X != m_hover_pixel.X || prev.Y != m_hover_pixel.Y)
			m_img_dlg.Invalidate(FALSE);
	}

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

void CSCCapturedNoteDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	CDialog::OnMouseMove(nFlags, point);
}
