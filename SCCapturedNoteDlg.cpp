// SCCapturedNoteDlg.cpp

#include "pch.h"
#include "SCCapturedNoteDlg.h"
#include "Common/Functions.h"
#include "Common/win_compat/dwm.h"
#include "Common/CDialog/CSCColorPicker/SCColorPicker.h"

//노트 배경은 노트 하나가 아니라 앱 전체 설정 — 여기서 바꾼 값이 이후 캡처 노트에도 그대로 적용된다.
//저장값은 ARGB. 알파 0 은 색으로서 의미가 없으므로 그 자리를 모드 표시로 쓴다.
static const DWORD back_default = 0x00000000;	//아래 back_default_color
static const DWORD back_zigzag  = 0x00000001;	//배경 전체를 투명 격자로

//불투명 배경은 캡처된 이미지의 일부인지 노트 배경인지 구분이 안 되므로, 처음부터 반투명으로 시작한다.
//알파가 255 미만이면 CSCD2ImageDlg 가 투명 격자를 깔고 그 위에 이 색을 덮는다.
//192 로 두면 격자 대비가 55 → 14 단계로 줄어 거의 안 보인다. 128 이면 격자가 또렷해 한눈에 배경으로 읽힌다.
static const Gdiplus::Color back_default_color = Gdiplus::Color(128, 32, 32, 32);

//키 이름이 note_back_color 에서 바뀐 이유: 예전 값은 COLORREF + 음수 sentinel 이라 ARGB 로 읽으면
//알파가 엉뚱하게 해석된다. 새 키로 시작하면 기존 값이 조용히 무시되고 기본값부터 다시 쌓인다.
static DWORD load_note_back_setting()
{
	return (DWORD)AfxGetApp()->GetProfileInt(_T("settings"), _T("note_back_argb"), (int)back_default);
}

static void save_note_back_setting(DWORD value)
{
	AfxGetApp()->WriteProfileInt(_T("settings"), _T("note_back_argb"), (int)value);
}

//크기 / 비율 / 마우스 픽셀 좌표 표시 여부. 가운데 버튼 토글 결과가 이후 노트에도 이어진다.
static bool load_note_show_info()
{
	return AfxGetApp()->GetProfileInt(_T("settings"), _T("note_show_info"), 1) != 0;
}

static void save_note_show_info(bool show)
{
	AfxGetApp()->WriteProfileInt(_T("settings"), _T("note_show_info"), show ? 1 : 0);
}

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
	ON_WM_NCMBUTTONDOWN()
	ON_WM_MBUTTONDOWN()
	ON_WM_NCHITTEST()
	ON_WM_NCCALCSIZE()
	ON_WM_CONTEXTMENU()
	ON_WM_NCRBUTTONUP()
	ON_WM_NCACTIVATE()
	ON_WM_NCMOUSEMOVE()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
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

double CSCCapturedNoteDlg::calc_client_size_for_image(const CRect& rc_monitor, int& cx, int& cy) const
{
	const int max_cx = rc_monitor.Width() * 2 / 3;
	const int max_cy = rc_monitor.Height() * 2 / 3;

	double scale = 1.0;
	if (m_img_w > max_cx)
		scale = static_cast<double>(max_cx) / m_img_w;
	if (m_img_h * scale > max_cy)
		scale = static_cast<double>(max_cy) / m_img_h;

	cx = static_cast<int>(m_img_w * scale + 0.5);
	cy = static_cast<int>(m_img_h * scale + 0.5);
	if (cx < 80) cx = 80;
	if (cy < 60) cy = 60;

	return scale;
}

//20260904 by claude. zoom(double) 의 하한이 0.2 라, 그보다 더 줄여야 하는 초대형 캡처(멀티모니터 전체 등)만
//fit2ctrl 에 맡긴다 — 그 경우 고정 배율로 두면 이미지가 창 밖으로 잘린다.
void CSCCapturedNoteDlg::apply_display_scale(double scale)
{
	if (scale >= 0.2)
		m_img_dlg.zoom(scale);
	else
		m_img_dlg.fit2ctrl(true);
}

//20260904 by claude. 한 번 재서 한 번 보정하면 어긋난다 — CreateEx 시점의 WM_NCCALCSIZE 는 아직 이 클래스의
//핸들러를 타지 않아 기본 프레임(위/아래 7px)으로 잡히는데, 그 뒤로는 OnNcCalcSize 의 "위쪽 NC 는 0" 규칙이
//적용된다. 첫 측정값으로 보정하면 그 차이만큼 client 가 커진 채 남는다
//(실측: 208 요청 → client 214, 이미지 위아래에 3px 씩 빈 배경이 보였다).
//프레임 규칙을 코드로 흉내내지 말고, 남은 차이만큼 다시 보정해 수렴시킨다.
void CSCCapturedNoteDlg::resize_client_to(int cx, int cy, const POINT* pos_client_screen)
{
	for (int i = 0; i < 3; ++i)
	{
		CRect rc_window, rc_client;
		GetWindowRect(rc_window);
		GetClientRect(rc_client);

		CPoint pt_client_origin(0, 0);
		ClientToScreen(&pt_client_origin);

		int win_x = rc_window.left;
		int win_y = rc_window.top;
		if (pos_client_screen)
		{
			win_x = pos_client_screen->x - (pt_client_origin.x - rc_window.left);
			win_y = pos_client_screen->y - (pt_client_origin.y - rc_window.top);
		}

		const int diff_cx = cx - rc_client.Width();
		const int diff_cy = cy - rc_client.Height();

		if (diff_cx == 0 && diff_cy == 0 && win_x == rc_window.left && win_y == rc_window.top)
			break;

		SetWindowPos(NULL,
			win_x, win_y,
			rc_window.Width() + diff_cx,
			rc_window.Height() + diff_cy,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
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

	//시작 배율 = 100%, 단 화면의 2/3 를 넘으면 ratio 보존 축소.
	//모니터 전체 캡처처럼 화면을 거의 채우는 이미지를 100% 로 띄우면 원본과 겹쳐 구분이 안 되고
	//노트를 옮기거나 닫기도 불편하다. 그 크기부터는 배율을 낮춰 "따로 뜬 창" 으로 보이게 한다.
	//여기서 구하는 값은 창 크기가 아니라 client 크기다 — WS_SIZEBOX 프레임 두께만큼 창을 더 키워야
	//이미지가 100% 로 그려진다. 창 크기로 잡으면 프레임이 먹은 만큼 축소돼 보인다 (아래 SetWindowPos).
	//
	//20260904 by claude. 기준 모니터는 노트가 뜰 자리의 모니터다. GetSystemMetrics(SM_CXSCREEN) 은
	//주 모니터만 알려줘서, 더 큰 다른 모니터에서 캡처해도 주 모니터의 2/3 로 잘렸다.
	CPoint pt_anchor;
	if (pos_screen)
		pt_anchor = CPoint(pos_screen->x, pos_screen->y);
	else
		::GetCursorPos(&pt_anchor);

	const CRect rc_monitor = get_monitor_rect(get_monitor_index(pt_anchor.x, pt_anchor.y));

	int win_cx = 0;
	int win_cy = 0;
	const double scale = calc_client_size_for_image(rc_monitor, win_cx, win_cy);

	//20260904 by claude. 캡처한 자리에 그대로 겹쳐 띄운다. 예전엔 "원본과 구분되게" 우하로 16px 밀고
	//테두리를 깜빡였는데, 창 그림자만으로 캡처본임이 충분히 드러나고 깜빡임은 flickering 처럼 보였다.
	int x, y;
	if (pos_screen)
	{
		x = pos_screen->x;
		y = pos_screen->y;
	}
	else
	{
		x = rc_monitor.left + (rc_monitor.Width() - win_cx) / 2;
		y = rc_monitor.top + (rc_monitor.Height() - win_cy) / 2;
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

	//WS_SIZEBOX 프레임이 client 를 갉아먹어 이미지가 100% 로 안 그려진다.
	//client 가 정확히 win_cx x win_cy 가 되도록 창을 키우고, client 좌상단이 의도한 좌표(x, y)에 둔다.
	{
		const POINT pt_client = { x, y };
		resize_client_to(win_cx, win_cy, &pt_client);
	}

	//WS_EX_LAYERED 활성화 ? Ctrl+wheel 로 창 투명도 조절 가능하게.
	ModifyStyleEx(0, WS_EX_LAYERED);
	SetLayeredWindowAttributes(0, m_alpha, LWA_ALPHA);

	//borderless 창은 기본적으로 그림자가 없다. 1px extend 로 DWM 이 그림자를 계산하게 한다.
	//캡처본이 원본 위에 떠 있다는 유일한 신호이므로 그림자는 반드시 있어야 한다.
	win_compat::dwm::extend_frame_into_client_area(m_hWnd);

	//20260904 by claude. 테두리는 캡처본과 배경을 가르는 경계선이라 색을 OS 에 맡기지 않고 직접 준다
	//— 기본값은 테마·강조색·활성/비활성에 따라 달라져 노트마다 외관이 달라진다.
	//값 (66,67,70) 은 이 환경의 OS 기본 테두리를 흰 이미지로 실측해 얻은 색이다.
	//밝은 회색(160,160,160)을 줘 봤더니 얇고 어둡던 선이 굵은 흰 선처럼 보여 되돌렸다.
	win_compat::dwm::set_border_color(m_hWnd, RGB(66, 67, 70));

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
	apply_back_setting(load_note_back_setting());
	m_show_info = load_note_show_info();

	m_img_dlg.set_image(&m_image);

	//20260904 by claude. 시작 모드는 fit2ctrl(창에 맞춤) 이 아니라 고정 배율이다.
	//fit2ctrl 은 창을 리사이즈할 때마다 이미지를 늘려 픽셀이 뭉개진다 — 캡처를 보는 창에서는
	//"지금 보는 것이 몇 배" 인지가 유지돼야 한다.
	apply_display_scale(scale);

	//post-paint 콜백 ? m_img_dlg 의 D2D 같은 frame 에 추가 오버레이 그림 (안티앨리어싱, z-order 충돌 없음).
	//본문은 멤버 함수로 분리. 람다는 this 캡처하여 멤버 호출만 위임.
	m_img_dlg.set_post_paint_callback(
		[this](ID2D1DeviceContext* d2dc) { on_img_dlg_post_paint(d2dc); });

	m_initialized = true;

	ShowWindow(SW_SHOW);
	SetForegroundWindow();

	return true;
}

void CSCCapturedNoteDlg::toggle_info()
{
	m_show_info = !m_show_info;
	save_note_show_info(m_show_info);
	m_img_dlg.Invalidate(FALSE);
}

void CSCCapturedNoteDlg::OnNcMButtonDown(UINT nHitTest, CPoint point)
{
	toggle_info();
	CDialog::OnNcMButtonDown(nHitTest, point);
}

void CSCCapturedNoteDlg::OnMButtonDown(UINT nFlags, CPoint point)
{
	toggle_info();
	CDialog::OnMButtonDown(nFlags, point);
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

CRect CSCCapturedNoteDlg::get_close_button_rect() const
{
	CRect rc;
	GetClientRect(rc);

	//20260904 by claude. **DPI 로 스케일하지 않는다.** 이 창의 client 는 이미지 픽셀과 1:1 이라
	//배율이 달라도 320px 짜리 캡처는 어느 모니터에서든 320px 그대로다. 그 고정된 판 위의 버튼만
	//DPI 를 따라 커지면 비율이 깨진다 — 175% 에서 36px(노트 폭의 11.6%), 100% 에서 21px(6.6%).
	//메인 다이얼로그는 창 자체가 DPI 로 커지므로 버튼도 커지는 것이 맞고, 여기는 반대다.
	//크롬은 자기가 올라탄 판을 따라간다 — 여기서 판은 노트(=이미지 픽셀)다.
	const int sz = 21;
	const int margin = 3;
	return CRect(rc.right - sz - margin, margin, rc.right - margin, margin + sz);
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

	//(2) 우상단 닫기 버튼 영역 — pan/move 보다 우선해서 HTCLIENT 로 라우팅.
	//OnMouseMove / OnLButton* 가 호버 시각화 + 클릭 처리를 담당.
	CPoint pt_client(point);
	ScreenToClient(&pt_client);
	if (get_close_button_rect().PtInRect(pt_client))
		return HTCLIENT;

	//(3) 클라이언트 영역.
	//Shift 누른 상태면 HTCLIENT 로 자식 (m_img_dlg) 이 받아 pan.
	//그 외에는 HTCAPTION 반환 → Windows DefWindowProc 가 modal move loop 자동 처리.
	//HTCAPTION 분기는 ASee 가 검증한 가장 신뢰성 높은 창 이동 경로.
	if (::GetAsyncKeyState(VK_SHIFT) & 0x8000)
		return HTCLIENT;

	return HTCAPTION;
}

void CSCCapturedNoteDlg::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
{
	//캡션바 없는 popup 은 default 처리가 상단에 흰색 NC 영역을 남긴다. 그 영역을 client 로 흡수해
	//우리가 덮어 그린다.
	//
	//20260904 by claude. 원래 `rgrc[0].top -= 6` 이었는데 그 6 이 하드코딩이라 DPI 를 못 따라갔다.
	//프레임 두께는 Per-Monitor V2 에서 모니터마다 다르다 — 100% 는 7px, 175% 는 11px.
	//6 을 빼면 100% 에서는 1px 만 남아 맞지만 175% 에서는 5px 이 남고 그 중 3px 이 흰색으로 보인다.
	//그래서 상수를 빼는 대신 *실제 프레임을 재서* 흡수한다:
	//  진입 시 rgrc[0] = 새 윈도우 rect → DefWindowProc 이 제자리에서 client rect 로 바꾼다.
	//  그 전후 차이가 곧 프레임 두께이므로, base 호출 뒤에 top 을 윈도우 top + 1 로 되돌리면 된다.
	//
	//20260904 by claude. **이 1px 을 없애면 창 그림자가 사라진다.** 위쪽 NC 를 0 으로 만들어 봤더니
	//DWM 이 프레임 자체를 그리지 않게 되어 그림자까지 같이 사라졌다. 그래서 1px 은 유지한다.
	//이 자리에 DWM 이 그리는 테두리 색은 init_with_image 에서 직접 지정한다.
	const LONG window_top = (bCalcValidRects && lpncsp) ? lpncsp->rgrc[0].top : 0;

	CDialog::OnNcCalcSize(bCalcValidRects, lpncsp);

	if (bCalcValidRects && lpncsp)
		lpncsp->rgrc[0].top = window_top + 1;
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

void CSCCapturedNoteDlg::apply_back_setting(DWORD value)
{
	m_img_dlg.set_back_zigzag(value == back_zigzag);

	if (value == back_zigzag)
		m_img_dlg.set_back_color(Gdiplus::Color::Transparent);
	else if (value == back_default)
		m_img_dlg.set_back_color(back_default_color);
	else
		m_img_dlg.set_back_color(Gdiplus::Color(value));
}

DWORD CSCCapturedNoteDlg::get_back_setting() const
{
	if (m_img_dlg.get_back_zigzag())
		return back_zigzag;

	const Gdiplus::Color cr = m_img_dlg.get_back_color();
	if (cr.GetValue() == back_default_color.GetValue())
		return back_default;

	return cr.GetValue();
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

	const bool is_nearest = (m_img_dlg.get_interpolation_mode() == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

	CMenu menu_interp;
	menu_interp.CreatePopupMenu();
	menu_interp.AppendMenu(MF_STRING | (is_nearest ? MF_CHECKED : MF_UNCHECKED), cmd_interp_nearest, _T("원본 그대로\tAlt+1"));
	menu_interp.AppendMenu(MF_STRING | (is_nearest ? MF_UNCHECKED : MF_CHECKED), cmd_interp_linear,  _T("부드럽게\tAlt+2"));
	menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(menu_interp.GetSafeHmenu()), _T("보간 방식(&Q)"));
	menu_interp.Detach();

	menu.AppendMenu(MF_SEPARATOR);

	const DWORD back = get_back_setting();
	const bool back_is_custom = (back != back_default && back != back_zigzag);

	CMenu menu_back;
	menu_back.CreatePopupMenu();
	menu_back.AppendMenu(MF_STRING | (back == back_default ? MF_CHECKED : MF_UNCHECKED), cmd_back_default, _T("기본"));
	menu_back.AppendMenu(MF_STRING | (back == back_zigzag  ? MF_CHECKED : MF_UNCHECKED), cmd_back_zigzag,  _T("투명 격자"));
	menu_back.AppendMenu(MF_STRING | (back_is_custom       ? MF_CHECKED : MF_UNCHECKED), cmd_back_custom,  _T("색 지정..."));
	menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(menu_back.GetSafeHmenu()), _T("배경(&B)"));

	//AppendMenu(MF_POPUP) 로 붙인 시점부터 소유권이 menu 로 넘어간다.
	//Detach 하지 않으면 menu_back 소멸자와 menu 소멸자가 같은 HMENU 를 두 번 파괴한다.
	menu_back.Detach();

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
			//100% = 이미지 픽셀 1:1 + 창 크기를 이미지 크기에 맞춰 자동 조정.
			//20260904 by claude. 기준은 처음 뜰 때와 같다 — 노트가 지금 올라가 있는 모니터의 2/3.
			//예전엔 주 모니터의 80% 였는데, 그러면 같은 이미지가 처음 뜰 때와 이 메뉴를 실행했을 때
			//서로 다른 크기가 됐다.
			CRect rc_window;
			GetWindowRect(rc_window);

			const CPoint pt_center = rc_window.CenterPoint();
			const CRect rc_monitor = get_monitor_rect(get_monitor_index(pt_center.x, pt_center.y));

			int target_cx = 0;
			int target_cy = 0;
			const double scale = calc_client_size_for_image(rc_monitor, target_cx, target_cy);

			apply_display_scale(scale);
			resize_client_to(target_cx, target_cy, NULL);
			break;
		}
		case cmd_zoom_fit:
			//창에 맞춤 = fit2ctrl(true). zoom() 과 별개의 모드 (m_fit2ctrl flag).
			m_img_dlg.fit2ctrl(true);
			break;
		case cmd_close:
			DestroyWindow();
			break;

		case cmd_interp_nearest:
			m_img_dlg.set_interpolation_mode(D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
			break;

		case cmd_interp_linear:
			m_img_dlg.set_interpolation_mode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
			break;

		case cmd_back_default:
			apply_back_setting(back_default);
			save_note_back_setting(back_default);
			break;

		case cmd_back_zigzag:
			apply_back_setting(back_zigzag);
			save_note_back_setting(back_zigzag);
			break;

		case cmd_back_custom:
		{
			const DWORD back = get_back_setting();
			const Gdiplus::Color cr_init = (back == back_default || back == back_zigzag)
				? back_default_color
				: Gdiplus::Color(back);

			CSCColorPicker picker;
			if (picker.DoModal(this, cr_init, _T("노트 배경색")) == IDCANCEL)
				break;

			//알파 0 을 그대로 저장하면 back_default / back_zigzag 와 값이 겹친다. 그 경우는 완전 투명이나
			//다름없으니 투명 격자 모드로 보낸다.
			const Gdiplus::Color cr = picker.get_selected_color();
			const DWORD value = (cr.GetA() == 0) ? back_zigzag : cr.GetValue();
			apply_back_setting(value);
			save_note_back_setting(value);
			break;
		}

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
		//TRACE(_T("PreTranslateMessage, WM_MOUSEMOVE\n"));
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
				execute_cmd(cmd_interp_nearest);
				return TRUE;
			case '2':
				execute_cmd(cmd_interp_linear);
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

void CSCCapturedNoteDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	//우상단 닫기 버튼이 HTCLIENT 로 라우팅되어 여기로 옴.
	if (get_close_button_rect().PtInRect(point))
	{
		m_close_btn_pressed = true;
		SetCapture();
		m_img_dlg.Invalidate(FALSE);
		return;
	}
	CDialog::OnLButtonDown(nFlags, point);
}

void CSCCapturedNoteDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_close_btn_pressed)
	{
		ReleaseCapture();
		const bool over = get_close_button_rect().PtInRect(point);
		m_close_btn_pressed = false;
		m_img_dlg.Invalidate(FALSE);
		//PostMessage 로 destroy 를 다음 메시지 사이클로 미룸 — OnLButtonUp 콜스택 안에서
		//self-delete 되면 CDialog::OnLButtonUp 복귀 시 freed this 접근 위험.
		if (over)
			PostMessage(WM_CLOSE);
		return;
	}
	CDialog::OnLButtonUp(nFlags, point);
}

void CSCCapturedNoteDlg::on_img_dlg_post_paint(ID2D1DeviceContext* d2dc)
{
	if (!d2dc)
		return;

	CRect rc;
	m_img_dlg.GetClientRect(&rc);

	const int margin = 6;

	if (m_show_info && m_img_w > 0 && m_img_h > 0)
	{
		WCHAR size_text[64];
		swprintf_s(size_text, L"(%d x %d) (%.3f:1)", m_img_w, m_img_h, double(m_img_w) / double(m_img_h));
		draw_text(d2dc, CRect(rc.left + margin, rc.top, rc.right, rc.bottom - margin), size_text,
			_T("Segoe UI"), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
			Gdiplus::Color::White, Gdiplus::Color::Black, Gdiplus::Color::Black, Gdiplus::Color::Transparent,
			1.0f, DT_LEFT | DT_BOTTOM);
	}

	if (m_show_info && m_hover_pixel.X >= 0.0f && m_hover_pixel.Y >= 0.0f)
	{
		WCHAR text[64];
		swprintf_s(text, L"(%d, %d)", int(m_hover_pixel.X), int(m_hover_pixel.Y));

		draw_text(d2dc, CRect(rc.left, rc.top, rc.right - margin, rc.bottom - margin), text,
			_T("Segoe UI"), 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
			Gdiplus::Color::White, Gdiplus::Color::Black, Gdiplus::Color::Black, Gdiplus::Color::Transparent,
			1.0f, DT_RIGHT | DT_BOTTOM);
	}

	//우상단 닫기 버튼 — 호버 시점에만 D2D 로 직접 그림. round 코너 바깥은 그리지 않아
	//이미지 / letterbox 가 자연스럽게 비치므로 배경색 매칭 문제 없음.
	if (m_close_btn_hover)
	{
		CRect rb = get_close_button_rect();
		const Gdiplus::Color cr_fill = m_close_btn_pressed
			? Gdiplus::Color(255, 180,  0,  0)	//pressed: 약간 어두운 빨강
			: Gdiplus::Color(255, 232, 17, 35);	//hover:   기본 빨강
		draw_rect(d2dc, rb, Gdiplus::Color::Transparent, cr_fill,
			0.0f, 0.0f, 4.0f, 0.0f, 0.0f);

		//홀수 폭(21) rect 의 진짜 중심은 10.5 — 정수 cp 로 그리면 0.5px 어긋남. float 좌표 사용.
		const float cx	 = rb.left + rb.Width()  * 0.5f;
		const float cy	 = rb.top  + rb.Height() * 0.5f;
		const float half = MIN(rb.Width(), rb.Height()) * 0.25f;
		draw_line(d2dc, cx - half, cy - half, cx + half, cy + half, Gdiplus::Color::White, 2.0f);
		draw_line(d2dc, cx + half, cy - half, cx - half, cy + half, Gdiplus::Color::White, 2.0f);
	}
}

void CSCCapturedNoteDlg::OnNcMouseMove(UINT nHitTest, CPoint point)
{
	//point 는 screen 좌표. OnNcHitTest 가 client 영역(닫기 버튼 제외)에 대해 HTCAPTION 을
	//리턴하므로 client 위 마우스 이동도 거의 여기로 옴 (resize 보더의 HTTOP/HTRIGHT 등 포함).
	//TRACE(_T("nc mouse move. hit=%u screen=(%d, %d)\n"), nHitTest, point.x, point.y);
	ScreenToClient(&point);

	//닫기 버튼 영역을 벗어나 NC 로 진입 — 호버 해제.
	if (m_close_btn_hover)
	{
		m_close_btn_hover = false;
		m_img_dlg.Invalidate(FALSE);
	}

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

void CSCCapturedNoteDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	//OnNcHitTest 가 닫기 버튼 영역에 대해 HTCLIENT 를 리턴하므로 그 영역의 이동이 여기로 옴.
	//호버 진입은 여기서, 호버 해제는 OnNcMouseMove 에서 — 영역을 벗어나는 순간 HTCAPTION 으로
	//바뀌어 NC 경로로 진입하기 때문.
	const bool over = get_close_button_rect().PtInRect(point);
	if (over != m_close_btn_hover)
	{
		m_close_btn_hover = over;
		m_img_dlg.Invalidate(FALSE);
	}
	CDialog::OnMouseMove(nFlags, point);
}
