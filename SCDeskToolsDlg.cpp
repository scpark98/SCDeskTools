
// SCDeskToolsDlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "SCDeskTools.h"
#include "SCDeskToolsDlg.h"
#include "afxdialogex.h"

#include <vector>

#include "Common/Functions.h"
#include "Common/CDialog/CSCColorPicker/SCDropperDlg.h"
#include "SCCaptureOverlayDlg.h"
#include "SCCapturedNoteDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// 응용 프로그램 정보에 사용되는 CAboutDlg 대화 상자입니다.

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 지원입니다.

// 구현입니다.
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CSCDeskToolsDlg 대화 상자



CSCDeskToolsDlg::CSCDeskToolsDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_SCDESKTOOLS_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CSCDeskToolsDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

UINT CSCDeskToolsDlg::s_msg_taskbar_created = ::RegisterWindowMessage(_T("TaskbarCreated"));

BEGIN_MESSAGE_MAP(CSCDeskToolsDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_MESSAGE(WM_SYSTRAYMSG, &CSCDeskToolsDlg::on_message_CSysTrayIcon)
	ON_REGISTERED_MESSAGE(Message_CSCColorPicker, &CSCDeskToolsDlg::on_message_CSCColorPicker)
	ON_REGISTERED_MESSAGE(s_msg_taskbar_created, &CSCDeskToolsDlg::OnTaskbarCreated)
	ON_COMMAND(ID_TOOL_COLOR_PICKER, &CSCDeskToolsDlg::OnToolColorPicker)
	ON_COMMAND(ID_TOOL_DROPPER, &CSCDeskToolsDlg::OnToolDropper)
	ON_COMMAND(ID_TOOL_CAPTURE_WINDOW, &CSCDeskToolsDlg::OnToolCaptureWindow)
	ON_COMMAND(ID_TOOL_CAPTURE_REGION, &CSCDeskToolsDlg::OnToolCaptureRegion)
	ON_COMMAND(ID_TOOL_PASTE_CLIPBOARD, &CSCDeskToolsDlg::OnToolPasteClipboard)
	ON_COMMAND(ID_APP_SHOW_HIDE, &CSCDeskToolsDlg::OnAppShowHide)
	ON_COMMAND(ID_HELP_ABOUT, &CSCDeskToolsDlg::OnAppAbout)
	ON_COMMAND(ID_APP_EXIT, &CSCDeskToolsDlg::OnAppExit)
	ON_BN_CLICKED(IDOK, &CSCDeskToolsDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CSCDeskToolsDlg::OnBnClickedCancel)
END_MESSAGE_MAP()


// CSCDeskToolsDlg 메시지 처리기

BOOL CSCDeskToolsDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 시스템 메뉴에 "정보..." 메뉴 항목을 추가합니다.

	// IDM_ABOUTBOX는 시스템 명령 범위에 있어야 합니다.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// 이 대화 상자의 아이콘을 설정합니다.  응용 프로그램의 주 창이 대화 상자가 아닐 경우에는
	//  프레임워크가 이 작업을 자동으로 수행합니다.
	SetIcon(m_hIcon, TRUE);			// 큰 아이콘을 설정합니다.
	SetIcon(m_hIcon, FALSE);		// 작은 아이콘을 설정합니다.

	SetWindowText(_T("SCDeskTools"));

	build_menus();

	m_sys_tray.SetParent(m_hWnd);
	HICON hIconTray = ::AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_sys_tray.CreateIcon(hIconTray, 1, _T("SCDeskTools"));
	m_sys_tray.ShowIcon(1);

	//modeless 컬러 피커. 처음에는 보이지 않게 두고, 메뉴 클릭 시 ShowWindow 로 토글한다.
	m_color_picker.create(this, _T("Color Picker"), false);

	return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void CSCDeskToolsDlg::build_menus()
{
	//단계 1: 코드 빌드 메뉴. 추후 리소스 메뉴로 옮길 수 있음.
	//Tools 서브메뉴를 한 번 만들어 메인 메뉴와 트레이 팝업이 동일하게 사용한다.
	CMenu menu_tools;
	menu_tools.CreatePopupMenu();
	menu_tools.AppendMenu(MF_STRING, ID_TOOL_COLOR_PICKER,    _T("컬러 피커(&C)..."));
	menu_tools.AppendMenu(MF_STRING, ID_TOOL_DROPPER,         _T("화면 돋보기(&M)..."));
	menu_tools.AppendMenu(MF_SEPARATOR);
	menu_tools.AppendMenu(MF_STRING, ID_TOOL_CAPTURE_WINDOW,  _T("창 캡처(&W)"));
	menu_tools.AppendMenu(MF_STRING, ID_TOOL_CAPTURE_REGION,  _T("영역 캡처(&R)"));
	menu_tools.AppendMenu(MF_SEPARATOR);
	menu_tools.AppendMenu(MF_STRING, ID_TOOL_PASTE_CLIPBOARD, _T("클립보드 이미지 띠우기(&V)\tCtrl+V"));

	CMenu menu_help;
	menu_help.CreatePopupMenu();
	menu_help.AppendMenu(MF_STRING, ID_HELP_ABOUT, _T("정보(&A)..."));

	m_menu_main.CreateMenu();
	m_menu_main.AppendMenu(MF_POPUP, (UINT_PTR)menu_tools.Detach(), _T("도구(&T)"));
	m_menu_main.AppendMenu(MF_POPUP, (UINT_PTR)menu_help.Detach(),  _T("도움말(&H)"));

	SetMenu(&m_menu_main);

	//메뉴바가 차지하는 높이만큼 다이얼로그 자체를 키워준다.
	//안 그러면 IDD 리소스에 정의된 클라이언트 영역이 그대로 유지되어
	//하단의 확인/취소 버튼이 메뉴바에 밀려 잘려 보인다.
	CRect rect_window;
	GetWindowRect(rect_window);
	int menu_height = GetSystemMetrics(SM_CYMENU);
	SetWindowPos(NULL, 0, 0,
		rect_window.Width(),
		rect_window.Height() + menu_height,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CSCDeskToolsDlg::show_tools_popup_menu(CPoint pt_screen)
{
	//트레이 우클릭 시 표시할 팝업. 메인 메뉴의 Tools 서브와 동일 항목 + 표시/숨김/종료.
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_APP_SHOW_HIDE,
		IsWindowVisible() ? _T("숨기기(&H)") : _T("표시(&S)"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_TOOL_COLOR_PICKER,   _T("컬러 피커(&C)..."));
	menu.AppendMenu(MF_STRING, ID_TOOL_DROPPER,        _T("화면 돋보기(&M)..."));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_TOOL_CAPTURE_WINDOW, _T("창 캡처(&W)"));
	menu.AppendMenu(MF_STRING, ID_TOOL_CAPTURE_REGION, _T("영역 캡처(&R)"));
	menu.AppendMenu(MF_STRING, ID_TOOL_PASTE_CLIPBOARD, _T("클립보드 이미지 띠우기(&V)"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_HELP_ABOUT,           _T("정보(&A)..."));
	menu.AppendMenu(MF_STRING, ID_APP_EXIT,            _T("종료(&X)"));

	//트레이 팝업 표준 처리: 메뉴 띄우기 전 SetForegroundWindow + 메뉴 항목 단축키 동작 보장.
	SetForegroundWindow();
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt_screen.x, pt_screen.y, this);
	PostMessage(WM_NULL);
}

void CSCDeskToolsDlg::toggle_main_window()
{
	if (IsWindowVisible() && !IsIconic())
	{
		ShowWindow(SW_HIDE);
	}
	else
	{
		ShowWindow(SW_SHOW);
		ShowWindow(SW_RESTORE);
		SetForegroundWindow();
		SetActiveWindow();
	}
}

void CSCDeskToolsDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// 대화 상자에 최소화 단추를 추가할 경우 아이콘을 그리려면
//  아래 코드가 필요합니다.  문서/뷰 모델을 사용하는 MFC 애플리케이션의 경우에는
//  프레임워크에서 이 작업을 자동으로 수행합니다.

void CSCDeskToolsDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 그리기를 위한 디바이스 컨텍스트입니다.

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 클라이언트 사각형에서 아이콘을 가운데에 맞춥니다.
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 아이콘을 그립니다.
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// 사용자가 최소화된 창을 끄는 동안에 커서가 표시되도록 시스템에서
//  이 함수를 호출합니다.
HCURSOR CSCDeskToolsDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CSCDeskToolsDlg::OnClose()
{
	//[X] 또는 Alt+F4 → 종료가 아니라 트레이로 숨김.
	//앱 종료는 트레이 팝업의 "종료" 또는 ID_APP_EXIT 명령으로만.
	ShowWindow(SW_HIDE);
}

void CSCDeskToolsDlg::OnDestroy()
{
	m_sys_tray.DeleteIcon(1);
	CDialogEx::OnDestroy();
}

LRESULT CSCDeskToolsDlg::on_message_CSysTrayIcon(WPARAM wParam, LPARAM lParam)
{
	switch (lParam)
	{
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
		toggle_main_window();
		break;

	case WM_RBUTTONUP:
	{
		CPoint pt;
		GetCursorPos(&pt);
		show_tools_popup_menu(pt);
		break;
	}
	}
	return 0;
}

LRESULT CSCDeskToolsDlg::OnTaskbarCreated(WPARAM, LPARAM)
{
	//Shell_TrayWnd 가 (재)생성될 때 모든 top-level 창에 브로드캐스트.
	//시작프로그램 부팅 시 셸이 늦거나 Explorer 크래시 후 재시작에 대비.
	m_sys_tray.DeleteIcon(1);

	HICON hIconTray = ::AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_sys_tray.CreateIcon(hIconTray, 1, _T("SCDeskTools"));
	m_sys_tray.ShowIcon(1);
	return 0;
}

void CSCDeskToolsDlg::OnToolColorPicker()
{
	//modeless 토글: 이미 떠 있으면 숨기고, 숨어있으면 띄운다.
	if (m_color_picker.GetSafeHwnd() == NULL)
		return;

	if (m_color_picker.IsWindowVisible())
	{
		m_color_picker.ShowWindow(SW_HIDE);
	}
	else
	{
		m_color_picker.ShowWindow(SW_SHOW);
		m_color_picker.SetForegroundWindow();
	}
}

LRESULT CSCDeskToolsDlg::on_message_CSCColorPicker(WPARAM wParam, LPARAM lParam)
{
	//modeless 피커가 색상 변경을 알릴 때마다 도착. 단계 1 에서는 타이틀바에 hex 만 반영.
	auto msg = reinterpret_cast<CSCColorPickerMessage*>(wParam);
	m_cr_selected = msg->cr_selected;

	CString title;
	title.Format(_T("SCDeskTools - #%02X%02X%02X"),
		m_cr_selected.GetR(), m_cr_selected.GetG(), m_cr_selected.GetB());
	SetWindowText(title);
	return 0;
}

void CSCDeskToolsDlg::OnToolDropper()
{
	//트레이/메인 메뉴가 화면에서 완전히 사라진 후 캡처되어야 하므로 약간 지연.
	//SCDropperDlg 헤더의 호출 예시 그대로 (CSCShapeDlg 와 달리 자체 메시지 루프 필요).
	Wait(200);

	CSCDropperDlg dlg;
	dlg.create(this);

	MSG msg = {};
	bool quit_posted = false;
	while (dlg.GetSafeHwnd() != NULL)
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
		if (dlg.GetSafeHwnd() != NULL)
			WaitMessage();
	}

	if (quit_posted)
	{
		PostQuitMessage(static_cast<int>(msg.wParam));
		return;
	}

	if (!dlg.is_picked())
		return;

	Gdiplus::Color cr = dlg.get_picked_color();
	CString s;
	s.Format(_T("스포이드 결과: #%02X%02X%02X"), cr.GetR(), cr.GetG(), cr.GetB());
	AfxMessageBox(s);
}

void CSCDeskToolsDlg::OnToolCaptureWindow()
{
	//우리 자신이 캡처에 들어가지 않게 메인/피커를 잠시 숨김.
	bool main_was_visible   = (IsWindowVisible() && !IsIconic()) ? true : false;
	bool picker_was_visible = (m_color_picker.GetSafeHwnd() && m_color_picker.IsWindowVisible()) ? true : false;

	if (main_was_visible)
		ShowWindow(SW_HIDE);
	if (picker_was_visible)
		m_color_picker.ShowWindow(SW_HIDE);

	//창들이 실제로 사라질 시간을 준다 (DWM 합성 1~2 프레임).
	Wait(200);

	CSCCaptureOverlayDlg dlg;
	bool created = dlg.create(this);

	if (created)
	{
		//SCDropperDlg 와 동일한 self-message-loop. 오버레이가 destroy 될 때까지 대기.
		MSG msg = {};
		bool quit_posted = false;
		while (dlg.GetSafeHwnd() != NULL)
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
			if (dlg.GetSafeHwnd() != NULL)
				WaitMessage();
		}

		//숨겼던 창 복원
		if (main_was_visible)
			ShowWindow(SW_SHOW);
		if (picker_was_visible)
			m_color_picker.ShowWindow(SW_SHOW);

		if (quit_posted)
		{
			PostQuitMessage(static_cast<int>(msg.wParam));
			return;
		}

		if (dlg.is_picked())
		{
			HWND hwnd = dlg.get_picked_hwnd();
			CRect rc_highlight = dlg.get_picked_rect_screen();
			CRect rc_virtual   = dlg.get_virtual_screen_rect();

			//1) PrintWindow 시도. 결과 DIB 는 클립보드용으로 살아있게 유지 (성공 시 클립보드 소유권 이전).
			CRect rc_window;
			::GetWindowRect(hwnd, &rc_window);

			int pw_w = rc_window.Width();
			int pw_h = rc_window.Height();
			bool pw_ok = false;
			int  pw_nonblack_pct = 0;
			HBITMAP hbmp_pw = NULL;

			if (pw_w > 0 && pw_h > 0)
			{
				HDC hdc_screen = ::GetDC(NULL);
				HDC hdc_mem = ::CreateCompatibleDC(hdc_screen);

				BITMAPINFO bmi = {};
				bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
				bmi.bmiHeader.biWidth       = pw_w;
				bmi.bmiHeader.biHeight      = -pw_h;
				bmi.bmiHeader.biPlanes      = 1;
				bmi.bmiHeader.biBitCount    = 32;
				bmi.bmiHeader.biCompression = BI_RGB;

				void* bits = nullptr;
				hbmp_pw = ::CreateDIBSection(hdc_mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
				HGDIOBJ old = ::SelectObject(hdc_mem, hbmp_pw);

				BOOL ok = ::PrintWindow(hwnd, hdc_mem, PW_RENDERFULLCONTENT);

				if (ok && bits)
				{
					BYTE* p = static_cast<BYTE*>(bits);
					int total = pw_w * pw_h;
					int step = (total > 4096) ? (total / 4096) : 1;
					int sampled = 0, nonblack = 0;
					for (int i = 0; i < total; i += step)
					{
						sampled++;
						BYTE b = p[i * 4 + 0];
						BYTE g = p[i * 4 + 1];
						BYTE r = p[i * 4 + 2];
						if (r > 8 || g > 8 || b > 8)
							nonblack++;
					}
					pw_nonblack_pct = sampled ? (nonblack * 100 / sampled) : 0;
					pw_ok = (pw_nonblack_pct >= 5);

					//alpha 채널이 0 이면 일부 paste target 에서 투명하게 보임. 0xFF 로 채움.
					if (pw_ok)
					{
						for (int i = 0; i < total; ++i)
							p[i * 4 + 3] = 0xFF;
					}
				}

				::SelectObject(hdc_mem, old);
				::DeleteDC(hdc_mem);
				::ReleaseDC(NULL, hdc_screen);
			}

			//2) PrintWindow 실패 시 → 프리즈 캡처에서 sub-region BitBlt 로 폴백 HBITMAP 생성.
			HBITMAP hbmp_fallback = NULL;
			if (!pw_ok)
			{
				HBITMAP hbmp_src = dlg.get_frozen_hbitmap();
				int dst_w = rc_highlight.Width();
				int dst_h = rc_highlight.Height();

				if (hbmp_src && dst_w > 0 && dst_h > 0)
				{
					HDC hdc_screen = ::GetDC(NULL);
					HDC hdc_src = ::CreateCompatibleDC(hdc_screen);
					HDC hdc_dst = ::CreateCompatibleDC(hdc_screen);

					BITMAPINFO bmi = {};
					bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
					bmi.bmiHeader.biWidth       = dst_w;
					bmi.bmiHeader.biHeight      = -dst_h;
					bmi.bmiHeader.biPlanes      = 1;
					bmi.bmiHeader.biBitCount    = 32;
					bmi.bmiHeader.biCompression = BI_RGB;

					void* dst_bits = nullptr;
					hbmp_fallback = ::CreateDIBSection(hdc_dst, &bmi, DIB_RGB_COLORS, &dst_bits, NULL, 0);
					HGDIOBJ old_src = ::SelectObject(hdc_src, hbmp_src);
					HGDIOBJ old_dst = ::SelectObject(hdc_dst, hbmp_fallback);

					//프리즈 DIB 좌표 = virtual screen 기준 0,0. screen rect 를 좌상단 기준 좌표로 변환.
					::BitBlt(hdc_dst, 0, 0, dst_w, dst_h,
						hdc_src, rc_highlight.left - rc_virtual.left,
						         rc_highlight.top  - rc_virtual.top,
						SRCCOPY);

					::SelectObject(hdc_src, old_src);
					::SelectObject(hdc_dst, old_dst);
					::DeleteDC(hdc_src);
					::DeleteDC(hdc_dst);
					::ReleaseDC(NULL, hdc_screen);
				}
			}

			//3) 클립보드 복사 — CF_DIB (HGLOBAL + BITMAPINFOHEADER + bottom-up 픽셀) 사용.
			//CF_BITMAP 단독으로 DIB section 을 넘기면 paste target 에 따라 빈 데이터로 보이는 이슈가 있어
			//그림판/Word/GIMP 등 대부분이 우선 사용하는 CF_DIB 로 보내는 것이 가장 호환성 좋다.
			HBITMAP hbmp_for_clip = pw_ok ? hbmp_pw : hbmp_fallback;
			int w_clip = pw_ok ? pw_w : rc_highlight.Width();
			int h_clip = pw_ok ? pw_h : rc_highlight.Height();
			bool clipboard_ok = false;

			if (hbmp_for_clip && w_clip > 0 && h_clip > 0)
			{
				DIBSECTION ds = {};
				if (::GetObject(hbmp_for_clip, sizeof(ds), &ds) == sizeof(ds) && ds.dsBm.bmBits)
				{
					const BYTE* src_bits = static_cast<const BYTE*>(ds.dsBm.bmBits);
					const int stride = w_clip * 4;
					const DWORD pixel_size = static_cast<DWORD>(stride) * h_clip;
					const DWORD total_size = sizeof(BITMAPINFOHEADER) + pixel_size;

					HGLOBAL hg_dib = ::GlobalAlloc(GHND, total_size);
					if (hg_dib)
					{
						BYTE* mem = static_cast<BYTE*>(::GlobalLock(hg_dib));
						BITMAPINFOHEADER* bih = reinterpret_cast<BITMAPINFOHEADER*>(mem);
						bih->biSize        = sizeof(BITMAPINFOHEADER);
						bih->biWidth       = w_clip;
						bih->biHeight      = h_clip;	//양수 = bottom-up (CF_DIB 표준)
						bih->biPlanes      = 1;
						bih->biBitCount    = 32;
						bih->biCompression = BI_RGB;
						bih->biSizeImage   = pixel_size;

						//소스는 top-down (CreateDIBSection 시 biHeight 음수). 행 순서 뒤집어 복사.
						BYTE* dst_pixels = mem + sizeof(BITMAPINFOHEADER);
						for (int y = 0; y < h_clip; ++y)
						{
							memcpy(dst_pixels + (h_clip - 1 - y) * stride,
								src_bits + y * stride, stride);
						}
						::GlobalUnlock(hg_dib);

						if (::OpenClipboard(m_hWnd))
						{
							::EmptyClipboard();
							HANDLE r = ::SetClipboardData(CF_DIB, hg_dib);
							::CloseClipboard();

							if (r != NULL)
								clipboard_ok = true;	//OS 가 hg_dib 소유권 가져감
							else
								::GlobalFree(hg_dib);	//실패 시 우리가 해제
						}
						else
						{
							::GlobalFree(hg_dib);
						}
					}
				}
			}

			//4) floating note (포스트잇) 띠움. HBITMAP DeleteObject 직전에 픽셀 데이터를 읽어서 spawn.
			//SCCapturedNoteDlg::spawn 은 내부에서 BGRA 를 D2D 비트맵으로 복사하므로 호출 후 HBITMAP 정리 가능.
			DIBSECTION ds_for_note = {};
			if (hbmp_for_clip && ::GetObject(hbmp_for_clip, sizeof(ds_for_note), &ds_for_note) == sizeof(ds_for_note)
				&& ds_for_note.dsBm.bmBits)
			{
				POINT pos = { rc_window.left, rc_window.top };
				CSCCapturedNoteDlg::spawn(
					static_cast<const BYTE*>(ds_for_note.dsBm.bmBits),
					w_clip, h_clip, &pos);
			}

			//우리가 소유한 HBITMAP 들은 클립보드 / 노트와 무관하게 모두 정리.
			if (hbmp_pw)       ::DeleteObject(hbmp_pw);
			if (hbmp_fallback) ::DeleteObject(hbmp_fallback);
		}
	}
	else
	{
		if (main_was_visible)
			ShowWindow(SW_SHOW);
		if (picker_was_visible)
			m_color_picker.ShowWindow(SW_SHOW);
		AfxMessageBox(_T("캡처 오버레이 생성 실패"));
	}
}

void CSCDeskToolsDlg::OnToolCaptureRegion()
{
	//TODO 단계 3: 풀스크린 layered overlay 로 영역 드래그 → CSCD2Image floating dlg.
	AfxMessageBox(_T("영역 캡처: 단계 3에서 구현"));
}

void CSCDeskToolsDlg::OnToolPasteClipboard()
{
	//클립보드의 이미지 (CF_DIB) 를 읽어 floating note 로 띠움.
	//테스트용으로 매번 캡처할 필요 없게 한다 — 다른 앱에서 Ctrl+C 한 이미지를 붙여넣기.
	if (!::OpenClipboard(m_hWnd))
	{
		AfxMessageBox(_T("클립보드 열기 실패"));
		return;
	}

	bool ok = false;
	int width = 0;
	int height = 0;
	std::vector<BYTE> bgra;	//top-down 32bpp BGRA

	HANDLE h_data = ::GetClipboardData(CF_DIB);
	if (h_data)
	{
		BYTE* mem = static_cast<BYTE*>(::GlobalLock(h_data));
		if (mem)
		{
			BITMAPINFOHEADER* bih = reinterpret_cast<BITMAPINFOHEADER*>(mem);
			const int w = bih->biWidth;
			const int h_signed = bih->biHeight;
			const int h = (h_signed < 0) ? -h_signed : h_signed;
			const bool src_top_down = (h_signed < 0);
			const int bpp = bih->biBitCount;

			//픽셀 데이터 시작 위치 = 헤더 크기 + 컬러 테이블 / BITFIELDS 마스크.
			DWORD pre_pixel = bih->biSize;
			if ((bpp == 16 || bpp == 32) && bih->biCompression == BI_BITFIELDS)
				pre_pixel += 3 * sizeof(DWORD);
			else if (bpp <= 8)
			{
				int colors = bih->biClrUsed ? (int)bih->biClrUsed : (1 << bpp);
				pre_pixel += colors * sizeof(RGBQUAD);
			}

			BYTE* pixels = mem + pre_pixel;
			//DIB 행은 4바이트 정렬.
			const int stride_src = ((w * bpp + 31) / 32) * 4;
			const int stride_dst = w * 4;

			if (w > 0 && h > 0 && (bpp == 32 || bpp == 24))
			{
				bgra.resize(static_cast<size_t>(stride_dst) * h);

				for (int y = 0; y < h; ++y)
				{
					int src_y = src_top_down ? y : (h - 1 - y);
					BYTE* src = pixels + src_y * stride_src;
					BYTE* dst = bgra.data() + y * stride_dst;

					if (bpp == 32)
					{
						memcpy(dst, src, stride_dst);
						//alpha 가 0 인 케이스 (BI_RGB 32bpp 는 X 채널이 미정) 보정.
						for (int x = 0; x < w; ++x)
							dst[x * 4 + 3] = 0xFF;
					}
					else // bpp == 24
					{
						for (int x = 0; x < w; ++x)
						{
							dst[x * 4 + 0] = src[x * 3 + 0];	//B
							dst[x * 4 + 1] = src[x * 3 + 1];	//G
							dst[x * 4 + 2] = src[x * 3 + 2];	//R
							dst[x * 4 + 3] = 0xFF;
						}
					}
				}

				width = w;
				height = h;
				ok = true;
			}

			::GlobalUnlock(h_data);
		}
	}

	::CloseClipboard();

	if (!ok)
	{
		AfxMessageBox(_T("클립보드에 이미지가 없거나 지원되지 않는 형식입니다.\n(24/32bpp DIB 만 지원)"));
		return;
	}

	CSCCapturedNoteDlg::spawn(bgra.data(), width, height, NULL);
}

void CSCDeskToolsDlg::OnAppShowHide()
{
	toggle_main_window();
}

void CSCDeskToolsDlg::OnAppAbout()
{
	CAboutDlg dlg;
	dlg.DoModal();
}

void CSCDeskToolsDlg::OnAppExit()
{
	EndDialog(IDOK);
}


void CSCDeskToolsDlg::OnBnClickedOk()
{
	ShowWindow(SW_HIDE);
}

void CSCDeskToolsDlg::OnBnClickedCancel()
{
	ShowWindow(SW_HIDE);
}
