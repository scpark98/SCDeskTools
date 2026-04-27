
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
#include "SCRegionCaptureDlg.h"
#include "SCProtractorDlg.h"
#include "SCRulerDlg.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//===== 툴 레지스트리 =====
//툴을 추가하려면 (1) Resource.h 에 ID_TOOL_* 정의, (2) 핸들러 함수 + ON_COMMAND 추가,
//(3) kTools 에 한 줄 추가, (4) 필요하면 kDefaultFavorites 에 ID 넣어 메인 창에 노출.
//build_buttons / show_tools_popup_menu 는 자동으로 신규 툴을 반영함.
namespace
{
	enum ToolCat
	{
		kCatCapture,
		kCatColor,
		kCatMeasure,
	};

	struct ToolItem
	{
		UINT		id;
		LPCTSTR		button_text;	//메인 창 버튼 라벨 (액셀러레이터 없음)
		LPCTSTR		menu_text;		//트레이 메뉴 라벨 (&X 액셀러레이터 포함)
		ToolCat		cat;
	};

	//menu_text 에는 더 이상 `(&X)` 액셀러레이터를 두지 않는다.
	//툴 항목은 모두 글로벌 단축키(kHotkeys) 가 있으므로 메뉴 표시 시점에 `\tAlt+Shift+X` 형태로 자동 부착.
	const ToolItem kTools[] =
	{
		//Capture
		{ ID_TOOL_CAPTURE_FULLSCREEN, _T("전체 화면 캡처"),       _T("전체 화면 캡처"),         kCatCapture },
		{ ID_TOOL_CAPTURE_WINDOW,     _T("창 캡처"),               _T("창 캡처"),                 kCatCapture },
		{ ID_TOOL_CAPTURE_REGION,     _T("영역 캡처"),             _T("영역 캡처"),               kCatCapture },
		{ ID_TOOL_PASTE_CLIPBOARD,    _T("클립보드 이미지 띠우기"),_T("클립보드 이미지 띠우기"),  kCatCapture },
		//Color
		{ ID_TOOL_COLOR_PICKER,       _T("컬러 피커..."),          _T("컬러 피커..."),            kCatColor },
		{ ID_TOOL_DROPPER,            _T("화면 돋보기..."),        _T("화면 돋보기..."),          kCatColor },
		//Measure
		{ ID_TOOL_PROTRACTOR,         _T("각도기"),                _T("각도기"),                  kCatMeasure },
		{ ID_TOOL_RULER,              _T("줄자"),                  _T("줄자"),                    kCatMeasure },
	};

	struct CategoryInfo
	{
		ToolCat		cat;
		LPCTSTR		title;
	};

	const CategoryInfo kCategories[] =
	{
		{ kCatCapture, _T("캡처(&P)") },
		{ kCatColor,   _T("색상(&C)") },
		{ kCatMeasure, _T("측정(&M)") },
	};

	//메인 창 기본 즐겨찾기. 사용자가 설정창에서 편집 시 레지스트리로 옮길 예정.
	const UINT kDefaultFavorites[] =
	{
		ID_TOOL_CAPTURE_FULLSCREEN,
		ID_TOOL_CAPTURE_WINDOW,
		ID_TOOL_CAPTURE_REGION,
		ID_TOOL_PASTE_CLIPBOARD,
		ID_TOOL_COLOR_PICKER,
		ID_TOOL_DROPPER,
		ID_TOOL_RULER,
		ID_TOOL_PROTRACTOR,
	};

	const ToolItem* find_tool(UINT id)
	{
		for (const ToolItem& t : kTools)
		{
			if (t.id == id)
				return &t;
		}
		return nullptr;
	}

	//===== 글로벌 단축키 =====
	//RegisterHotKey 으로 OS 에 등록 → WM_HOTKEY 로 수신 → wParam=id 로 tool_id 찾아 WM_COMMAND 위임.
	//ON_COMMAND 핸들러를 그대로 재사용하므로 새 단축키 추가 시 이 표에 한 줄만 추가.
	struct HotkeyItem
	{
		int			id;				//RegisterHotKey ID (per-window 1~0xBFFF)
		UINT		tool_id;		//ID_TOOL_* — WM_COMMAND 으로 dispatch
		UINT		modifiers;		//MOD_ALT | MOD_SHIFT | ... (MOD_NOREPEAT 권장)
		UINT		vkey;
		LPCTSTR		description;	//등록 실패 알림 / 향후 설정 다이얼로그용
	};

	//모니터 캡처 단축키는 모니터 개수에 따라 동적 — kHotkeys 와 ID 충돌 피하려고 100 base 사용.
	//Alt+Shift+'1'..'9' 까지 최대 9 개 모니터 지원.
	const int kMonitorHotkeyIdBase = 100;
	const int kMonitorHotkeyMaxCount = 9;

	const HotkeyItem kHotkeys[] =
	{
		{ 1, ID_TOOL_CAPTURE_WINDOW,     MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'S', _T("창 캡처 (Alt+Shift+S)") },
		{ 2, ID_TOOL_CAPTURE_FULLSCREEN, MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'F', _T("전체 화면 캡처 (Alt+Shift+F)") },
		{ 3, ID_TOOL_CAPTURE_REGION,     MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'R', _T("영역 캡처 (Alt+Shift+R)") },
		{ 4, ID_TOOL_PASTE_CLIPBOARD,    MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'V', _T("클립보드 이미지 띠우기 (Alt+Shift+V)") },
		{ 5, ID_TOOL_COLOR_PICKER,       MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'C', _T("컬러 피커 (Alt+Shift+C)") },
		{ 6, ID_TOOL_DROPPER,            MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'M', _T("화면 돋보기 (Alt+Shift+M)") },
		{ 7, ID_TOOL_PROTRACTOR,         MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'P', _T("각도기 (Alt+Shift+P)") },
		{ 8, ID_TOOL_RULER,              MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'L', _T("줄자 (Alt+Shift+L)") },
	};

	UINT find_tool_id_by_hotkey_id(int hotkey_id)
	{
		for (const HotkeyItem& h : kHotkeys)
		{
			if (h.id == hotkey_id)
				return h.tool_id;
		}
		return 0;
	}

	const HotkeyItem* find_hotkey_for_tool(UINT tool_id)
	{
		for (const HotkeyItem& h : kHotkeys)
		{
			if (h.tool_id == tool_id)
				return &h;
		}
		return nullptr;
	}

	//"Alt+Shift+F" 형태로 단축키 표시 문자열 생성. 향후 다른 modifier 조합 추가 시 그대로 동작.
	CString format_hotkey_text(UINT modifiers, UINT vkey)
	{
		CString s;
		if (modifiers & MOD_CONTROL) s += _T("Ctrl+");
		if (modifiers & MOD_ALT)     s += _T("Alt+");
		if (modifiers & MOD_SHIFT)   s += _T("Shift+");
		if (modifiers & MOD_WIN)     s += _T("Win+");

		if ((vkey >= 'A' && vkey <= 'Z') || (vkey >= '0' && vkey <= '9'))
		{
			s += (TCHAR)vkey;
		}
		else
		{
			//F1..F24, 기타 가상키 추가 시 케이스 확장. 지금은 unreachable.
			CString k;
			k.Format(_T("VK_0x%02X"), vkey);
			s += k;
		}
		return s;
	}

	CString hotkey_text_for_tool(UINT tool_id)
	{
		const HotkeyItem* h = find_hotkey_for_tool(tool_id);
		return h ? format_hotkey_text(h->modifiers, h->vkey) : CString();
	}
}


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

UINT CSCDeskToolsDlg::Message_TaskbarCreated = ::RegisterWindowMessage(_T("TaskbarCreated"));

BEGIN_MESSAGE_MAP(CSCDeskToolsDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
	ON_MESSAGE(WM_SYSTRAYMSG, &CSCDeskToolsDlg::on_message_CSysTrayIcon)
	ON_REGISTERED_MESSAGE(Message_CSCColorPicker, &CSCDeskToolsDlg::on_message_CSCColorPicker)
	ON_REGISTERED_MESSAGE(Message_TaskbarCreated, &CSCDeskToolsDlg::on_message_TaskbarCreated)
	ON_MESSAGE(Message_HideOnStartup, &CSCDeskToolsDlg::on_message_HideOnStartup)
	ON_MESSAGE(WM_CLIPBOARDUPDATE, &CSCDeskToolsDlg::OnClipboardUpdate)
	ON_MESSAGE(WM_HOTKEY, &CSCDeskToolsDlg::on_hotkey)
	ON_MESSAGE(WM_DISPLAYCHANGE, &CSCDeskToolsDlg::on_display_change)
	ON_COMMAND(ID_TOOL_COLOR_PICKER, &CSCDeskToolsDlg::OnToolColorPicker)
	ON_COMMAND(ID_TOOL_DROPPER, &CSCDeskToolsDlg::OnToolDropper)
	ON_COMMAND(ID_TOOL_CAPTURE_WINDOW, &CSCDeskToolsDlg::OnToolCaptureWindow)
	ON_COMMAND(ID_TOOL_CAPTURE_REGION, &CSCDeskToolsDlg::OnToolCaptureRegion)
	ON_COMMAND(ID_TOOL_CAPTURE_FULLSCREEN, &CSCDeskToolsDlg::OnToolCaptureFullscreen)
	ON_COMMAND_RANGE(ID_TOOL_CAPTURE_MONITOR_FIRST, ID_TOOL_CAPTURE_MONITOR_LAST, &CSCDeskToolsDlg::OnToolCaptureMonitor)
	ON_COMMAND(ID_TOOL_PASTE_CLIPBOARD, &CSCDeskToolsDlg::OnToolPasteClipboard)
	ON_COMMAND(ID_TOOL_PROTRACTOR, &CSCDeskToolsDlg::OnToolProtractor)
	ON_COMMAND(ID_TOOL_RULER, &CSCDeskToolsDlg::OnToolRuler)
	ON_COMMAND(ID_APP_SHOW_HIDE, &CSCDeskToolsDlg::OnAppShowHide)
	ON_COMMAND(ID_HELP_ABOUT, &CSCDeskToolsDlg::OnAppAbout)
	ON_COMMAND(ID_SC_EXIT, &CSCDeskToolsDlg::OnAppExit)
	ON_BN_CLICKED(IDOK, &CSCDeskToolsDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CSCDeskToolsDlg::OnBnClickedCancel)
	ON_WM_WINDOWPOSCHANGED()
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

	//기본 즐겨찾기 채우기 (추후: 레지스트리에서 읽기 → 설정창 편집 가능).
	m_favorites.assign(std::begin(kDefaultFavorites), std::end(kDefaultFavorites));

	//.rc 의 디폴트(480x325) 보다 작게 — 8 즐겨찾기 + 개발 종료 버튼이 깔끔히 들어가는 컴팩트 크기.
	//build_buttons 가 GetClientRect 로 배치하므로 반드시 그 전에 호출.
	{
		//높이 = top_margin(14) + 4행 * btn_h(50) + 3 gap * 10 + bottom_margin(14) = 258.
		//(개발 종료 버튼이 숨김 처리되어 그 공간을 그대로 도려냄.)
		const int target_client_w = 320;
		const int target_client_h = 258;
		CRect target(0, 0, target_client_w, target_client_h);
		::AdjustWindowRectEx(&target, GetStyle(), FALSE, GetExStyle());
		SetWindowPos(NULL, 0, 0, target.Width(), target.Height(),
			SWP_NOMOVE | SWP_NOZORDER);
	}

	build_buttons();
	build_exit_button();

	m_sys_tray.SetParent(m_hWnd);
	HICON hIconTray = ::AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_sys_tray.CreateIcon(hIconTray, 1, _T("SCDeskTools"));
	m_sys_tray.ShowIcon(1);

	//modeless 컬러 피커. 처음에는 보이지 않게 두고, 메뉴 클릭 시 ShowWindow 로 토글한다.
	m_color_picker.create(this, _T("Color Picker"), false);

	//클립보드 변경 감지: 이미지가 있을 때만 "클립보드 이미지 띠우기" 버튼/메뉴를 활성화.
	::AddClipboardFormatListener(m_hWnd);
	update_paste_clipboard_state();

	register_global_hotkeys();
	//프로그램 시작 시 모니터 정보 + 모니터 단축키 초기 등록.
	register_monitor_hotkeys();

	SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);

	RestoreWindowPosition(&theApp, this, _T(""), false, false);

	//트레이 상주 앱 — 시작 시 메인 창 숨김. 트레이 더블클릭/단축키/우클릭 메뉴로 표시.
	//OnInitDialog 안에서 ShowWindow(SW_HIDE) 호출하면 다이얼로그 매니저가 다시 보이게 만들 수 있어
	//PostMessage 로 첫 페인트 직후 시점에 숨김 처리. (브리프 플래시 가능하지만 허용)
	PostMessage(Message_HideOnStartup);

	return TRUE;  // 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void CSCDeskToolsDlg::build_buttons()
{
	//m_favorites 의 ID 들을 순서대로 텍스트 버튼으로 배치. ID 는 ON_COMMAND 가 자동 처리.
	//추후 아이콘 포함 버튼 (CSCButton 등) 으로 교체 가능.
	m_buttons_favorite.clear();

	CRect rc_client;
	GetClientRect(rc_client);

	const int margin = 14;
	const int gap_x  = 10;
	const int gap_y  = 10;
	const int cols   = 2;
	const int btn_w  = (rc_client.Width() - margin * 2 - gap_x * (cols - 1)) / cols;
	//두 줄 캡션 (기능명 / 글로벌 단축키) 수용 위해 한 줄짜리 36 → 50.
	const int btn_h  = 50;

	for (size_t i = 0; i < m_favorites.size(); ++i)
	{
		const ToolItem* tool = find_tool(m_favorites[i]);
		if (!tool)
			continue;

		const int row = int(i) / cols;
		const int col = int(i) % cols;
		const int x = margin + col * (btn_w + gap_x);
		const int y = margin + row * (btn_h + gap_y);

		//1행 = 기능명, 2행 = 글로벌 단축키. 단축키 미등록 항목은 1행만.
		CString caption = tool->button_text;
		CString hotkey  = hotkey_text_for_tool(tool->id);
		if (!hotkey.IsEmpty())
		{
			caption += _T("\n");
			caption += hotkey;
		}

		auto btn = std::make_unique<CButton>();
		CRect rc_btn(x, y, x + btn_w, y + btn_h);
		btn->Create(caption,
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_MULTILINE,
			rc_btn, this, tool->id);
		btn->SetFont(GetFont());
		m_buttons_favorite.push_back(std::move(btn));
	}
}

void CSCDeskToolsDlg::build_exit_button()
{
	//우측 하단 종료 버튼. ID_SC_EXIT 사용 → ON_COMMAND 가 OnAppExit (EndDialog) 으로 라우팅.
	//현재는 트레이/SC_MINIMIZE/[X] 흐름이 정착돼 평소엔 불필요 → WS_VISIBLE 제거로 숨김 처리.
	//다시 보이려면 아래 Create 의 스타일에 WS_VISIBLE 을 도로 추가 (또는 m_button_exit.ShowWindow(SW_SHOW)).
	CRect rc_client;
	GetClientRect(rc_client);

	const int margin = 14;
	const int btn_w  = 80;
	const int btn_h  = 32;

	CRect rc_btn(
		rc_client.right  - margin - btn_w,
		rc_client.bottom - margin - btn_h,
		rc_client.right  - margin,
		rc_client.bottom - margin);

	m_button_exit.Create(_T("종료"),
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
		rc_btn, this, ID_SC_EXIT);
	m_button_exit.SetFont(GetFont());
}

void CSCDeskToolsDlg::show_tools_popup_menu(CPoint pt_screen)
{
	//트레이 우클릭 시 표시할 팝업. 카테고리별 서브메뉴 + 메인 창 표시/숨김 + 정보/종료.
	//툴 항목은 kTools / kCategories 자동 반영 → 새 툴 추가 시 메뉴 코드 수정 불필요.
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_APP_SHOW_HIDE,
		IsWindowVisible() ? _T("숨기기(&H)") : _T("표시(&S)"));
	menu.AppendMenu(MF_SEPARATOR);

	for (const CategoryInfo& ci : kCategories)
	{
		CMenu sub;
		sub.CreatePopupMenu();
		for (const ToolItem& t : kTools)
		{
			if (t.cat != ci.cat)
				continue;
			UINT flags = MF_STRING;
			if (t.id == ID_TOOL_PASTE_CLIPBOARD && !clipboard_has_image())
				flags |= MF_GRAYED;

			//"기능명\tAlt+Shift+X" — Tab 이후 부분은 OS 가 메뉴 우측에 정렬해 표시.
			//단축키 미등록 항목은 Tab 자체를 생략.
			CString item_text = t.menu_text;
			CString hotkey    = hotkey_text_for_tool(t.id);
			if (!hotkey.IsEmpty())
			{
				item_text += _T("\t");
				item_text += hotkey;
			}
			sub.AppendMenu(flags, t.id, item_text);

			//전체 화면 캡처 바로 다음에 모니터별 캡처 서브메뉴 (모니터 2개 이상일 때만).
			if (t.id == ID_TOOL_CAPTURE_FULLSCREEN)
			{
				enum_display_monitors();	//Common: g_monitors 갱신
				if (g_monitors.size() >= 2)
				{
					CMenu sub_mon;
					sub_mon.CreatePopupMenu();
					const size_t max_n = std::min<size_t>(g_monitors.size(),
						ID_TOOL_CAPTURE_MONITOR_LAST - ID_TOOL_CAPTURE_MONITOR_FIRST + 1);
					for (size_t i = 0; i < max_n; ++i)
					{
						CString s;
						s.Format(_T("%d번 모니터 캡처"), int(i + 1));
						//모니터 단축키도 처음 9 개까지 Alt+Shift+1..9 로 표시.
						if (i < kMonitorHotkeyMaxCount)
							s.AppendFormat(_T("\tAlt+Shift+%d"), int(i + 1));
						sub_mon.AppendMenu(MF_STRING, ID_TOOL_CAPTURE_MONITOR_FIRST + UINT(i), s);
					}
					sub.AppendMenu(MF_POPUP, (UINT_PTR)sub_mon.Detach(), _T("모니터별 캡처"));
				}
			}
		}
		menu.AppendMenu(MF_POPUP, (UINT_PTR)sub.Detach(), ci.title);
	}

	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_HELP_ABOUT, _T("정보(&A)..."));
	menu.AppendMenu(MF_STRING, ID_SC_EXIT,   _T("종료(&X)"));

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
	else if ((nID & 0xFFF0) == SC_MINIMIZE)
	{
		//최소화 = 트레이로 숨김 (작업표시줄 점유 X). 트레이 더블클릭 / 글로벌 단축키로 복귀.
		ShowWindow(SW_HIDE);
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
	//앱 종료는 트레이 팝업의 "종료" 또는 ID_SC_EXIT 명령으로만.
	ShowWindow(SW_HIDE);
}

void CSCDeskToolsDlg::OnDestroy()
{
	unregister_monitor_hotkeys();
	unregister_global_hotkeys();
	::RemoveClipboardFormatListener(m_hWnd);
	m_sys_tray.DeleteIcon(1);
	CDialogEx::OnDestroy();
}

void CSCDeskToolsDlg::register_global_hotkeys()
{
	//다른 앱이 이미 잡고 있는 조합은 등록 실패 — 실패 항목만 모아 트레이 풍선으로 알림.
	//RegisterHotKey TRUE 인데도 WM_HOTKEY 가 안 오면 다른 앱의 WH_KEYBOARD_LL hook 가로채기.
	//(NVIDIA GeForce Experience, Xbox Game Bar, Discord, OBS 등이 흔한 범인.)
	CString failed;
	for (const HotkeyItem& h : kHotkeys)
	{
		BOOL ok = ::RegisterHotKey(m_hWnd, h.id, h.modifiers, h.vkey);
		DWORD err = ok ? 0 : ::GetLastError();
		TRACE(_T("[hotkey] RegisterHotKey id=%d %s -> %s (err=%lu)\n"),
			h.id, h.description, ok ? _T("OK") : _T("FAIL"), err);

		if (!ok)
		{
			if (!failed.IsEmpty())
				failed += _T(", ");
			failed += h.description;
		}
	}

	if (!failed.IsEmpty())
	{
		CString msg;
		msg.Format(_T("다음 단축키가 다른 앱과 충돌하여 등록되지 않았습니다:\n%s"), (LPCTSTR)failed);
		m_sys_tray.ShowBalloon(1, _T("SCDeskTools"), msg, NIIF_WARNING);
	}
}

void CSCDeskToolsDlg::unregister_global_hotkeys()
{
	//등록 실패한 ID 에 대해서도 UnregisterHotKey 는 무해 (FALSE 만 반환).
	for (const HotkeyItem& h : kHotkeys)
		::UnregisterHotKey(m_hWnd, h.id);
}

void CSCDeskToolsDlg::register_monitor_hotkeys()
{
	//기존 동적 등록분 해제 후 g_monitors 갱신 → 현재 개수만큼 재등록.
	//단일 모니터일 때는 등록 생략 (전체 화면 캡처와 동일하므로 단축키 낭비).
	unregister_monitor_hotkeys();

	enum_display_monitors();	//Common — g_monitors 채움
	if (g_monitors.size() < 2)
		return;

	const int n = static_cast<int>(std::min<size_t>(g_monitors.size(), kMonitorHotkeyMaxCount));
	for (int i = 0; i < n; ++i)
	{
		const UINT vkey = '1' + UINT(i);	//Alt+Shift+'1'..'9'
		const int  id   = kMonitorHotkeyIdBase + i;
		BOOL ok = ::RegisterHotKey(m_hWnd, id,
			MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, vkey);
		TRACE(_T("[hotkey] RegisterHotKey monitor %d Alt+Shift+%d -> %s (err=%lu)\n"),
			i + 1, i + 1, ok ? _T("OK") : _T("FAIL"), ok ? 0UL : ::GetLastError());
	}
	m_registered_monitor_count = n;
}

void CSCDeskToolsDlg::unregister_monitor_hotkeys()
{
	for (int i = 0; i < m_registered_monitor_count; ++i)
		::UnregisterHotKey(m_hWnd, kMonitorHotkeyIdBase + i);
	m_registered_monitor_count = 0;
}

LRESULT CSCDeskToolsDlg::on_hotkey(WPARAM wParam, LPARAM /*lParam*/)
{
	//기존 ON_COMMAND 핸들러로 위임 — 메뉴 / 버튼 / 단축키 단일 진입점 유지.
	const int hotkey_id = static_cast<int>(wParam);

	//1) 정적 툴 단축키 (kHotkeys, id 1..8)
	UINT tool_id = find_tool_id_by_hotkey_id(hotkey_id);
	if (tool_id != 0)
	{
		TRACE(_T("[hotkey] WM_HOTKEY id=%d -> tool_id=0x%X\n"), hotkey_id, tool_id);
		SendMessage(WM_COMMAND, MAKEWPARAM(tool_id, 0), 0);
		return 0;
	}

	//2) 동적 모니터 단축키 (kMonitorHotkeyIdBase + i)
	if (hotkey_id >= kMonitorHotkeyIdBase &&
		hotkey_id <  kMonitorHotkeyIdBase + kMonitorHotkeyMaxCount)
	{
		const int monitor_idx = hotkey_id - kMonitorHotkeyIdBase;
		const UINT mon_id = ID_TOOL_CAPTURE_MONITOR_FIRST + UINT(monitor_idx);
		TRACE(_T("[hotkey] WM_HOTKEY monitor idx=%d -> 0x%X\n"), monitor_idx, mon_id);
		SendMessage(WM_COMMAND, MAKEWPARAM(mon_id, 0), 0);
		return 0;
	}

	return 0;
}

LRESULT CSCDeskToolsDlg::on_display_change(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	//모니터 핫플러그·해상도 변경 — 모니터 캐시 갱신 + 단축키 재등록.
	//정적 툴 단축키(kHotkeys) 는 디스플레이 무관이라 재등록 불필요.
	register_monitor_hotkeys();
	return 0;
}

void CSCDeskToolsDlg::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	//메인 창 우클릭 = 트레이 우클릭과 동일한 카테고리 컨텍스트 메뉴.
	//point == (-1,-1) 이면 키보드 메뉴 키 → 창 중앙 사용.
	if (point.x == -1 && point.y == -1)
	{
		CRect rc;
		GetWindowRect(rc);
		point.x = rc.left + rc.Width()  / 2;
		point.y = rc.top  + rc.Height() / 2;
	}
	show_tools_popup_menu(point);
}

bool CSCDeskToolsDlg::clipboard_has_image() const
{
	//클립보드 열지 않고 즉시 확인 가능. CF_DIB 우선, 없으면 CF_BITMAP / CF_DIBV5 도 허용.
	return ::IsClipboardFormatAvailable(CF_DIB)
		|| ::IsClipboardFormatAvailable(CF_DIBV5)
		|| ::IsClipboardFormatAvailable(CF_BITMAP);
}

void CSCDeskToolsDlg::update_paste_clipboard_state()
{
	const BOOL enable = clipboard_has_image() ? TRUE : FALSE;
	for (auto& btn : m_buttons_favorite)
	{
		if (btn->GetSafeHwnd() && btn->GetDlgCtrlID() == ID_TOOL_PASTE_CLIPBOARD)
		{
			btn->EnableWindow(enable);
			return;
		}
	}
	//paste 버튼이 즐겨찾기에 없으면 메뉴에서만 상태 갱신 → show_tools_popup_menu 가 매번 검사.
}

LRESULT CSCDeskToolsDlg::OnClipboardUpdate(WPARAM, LPARAM)
{
	update_paste_clipboard_state();
	return 0;
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

LRESULT CSCDeskToolsDlg::on_message_TaskbarCreated(WPARAM, LPARAM)
{
	//Shell_TrayWnd 가 (재)생성될 때 모든 top-level 창에 브로드캐스트.
	//시작프로그램 부팅 시 셸이 늦거나 Explorer 크래시 후 재시작에 대비.
	m_sys_tray.DeleteIcon(1);

	HICON hIconTray = ::AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_sys_tray.CreateIcon(hIconTray, 1, _T("SCDeskTools"));
	m_sys_tray.ShowIcon(1);
	return 0;
}

LRESULT CSCDeskToolsDlg::on_message_HideOnStartup(WPARAM, LPARAM)
{
	ShowWindow(SW_HIDE);
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
	//돋보기가 화면 픽셀을 확대 표시 → 메인/피커가 보이면 돋보기 안에 자기 자신이 비춰져 부자연.
	//다른 캡처/오버레이 도구와 동일하게 HideFloating 으로 잠시 숨김 (RAII 가 함수 종료 시 자동 복원).
	HideFloating hide(this);

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

void CSCDeskToolsDlg::send_image_to_clipboard_and_note(const BYTE* bgra_top_down, int w, int h, POINT note_pos)
{
	//top-down BGRA 픽셀을 받아 (1) CF_DIB (bottom-up) 으로 클립보드 복사, (2) floating note 띠움.
	//창/영역/전체 화면 캡처가 동일 사용. 호출자는 픽셀 버퍼 소유권 그대로 유지.
	if (!bgra_top_down || w <= 0 || h <= 0)
		return;

	const int stride = w * 4;
	const DWORD pixel_size = static_cast<DWORD>(stride) * h;
	const DWORD total_size = sizeof(BITMAPINFOHEADER) + pixel_size;

	HGLOBAL hg_dib = ::GlobalAlloc(GHND, total_size);
	if (hg_dib)
	{
		BYTE* mem = static_cast<BYTE*>(::GlobalLock(hg_dib));
		BITMAPINFOHEADER* bih = reinterpret_cast<BITMAPINFOHEADER*>(mem);
		bih->biSize        = sizeof(BITMAPINFOHEADER);
		bih->biWidth       = w;
		bih->biHeight      = h;	//양수 = bottom-up (CF_DIB 표준)
		bih->biPlanes      = 1;
		bih->biBitCount    = 32;
		bih->biCompression = BI_RGB;
		bih->biSizeImage   = pixel_size;

		BYTE* dst_pixels = mem + sizeof(BITMAPINFOHEADER);
		for (int y = 0; y < h; ++y)
		{
			memcpy(dst_pixels + (h - 1 - y) * stride,
				bgra_top_down + y * stride, stride);
		}
		::GlobalUnlock(hg_dib);

		if (::OpenClipboard(m_hWnd))
		{
			::EmptyClipboard();
			HANDLE r = ::SetClipboardData(CF_DIB, hg_dib);
			::CloseClipboard();
			if (r == NULL)
				::GlobalFree(hg_dib);	//실패 시 우리가 해제
		}
		else
		{
			::GlobalFree(hg_dib);
		}
	}

	CSCCapturedNoteDlg::spawn(bgra_top_down, w, h, &note_pos);
}

CSCDeskToolsDlg::HideFloating::HideFloating(CSCDeskToolsDlg* d)
	: dlg(d)
{
	main_was_visible   = (d->IsWindowVisible() && !d->IsIconic()) ? true : false;
	picker_was_visible = (d->m_color_picker.GetSafeHwnd() && d->m_color_picker.IsWindowVisible()) ? true : false;

	if (main_was_visible)
		d->ShowWindow(SW_HIDE);
	if (picker_was_visible)
		d->m_color_picker.ShowWindow(SW_HIDE);
}

CSCDeskToolsDlg::HideFloating::~HideFloating()
{
	if (main_was_visible)
		dlg->ShowWindow(SW_SHOW);
	if (picker_was_visible)
		dlg->m_color_picker.ShowWindow(SW_SHOW);
}

void CSCDeskToolsDlg::capture_screen_rect(const CRect& rc_screen)
{
	//지정 screen rect 영역을 BitBlt 캡처 → CF_DIB 클립보드 복사 + floating note.
	//전체 화면 캡처 / 모니터별 캡처 가 공통 사용.
	const int w = rc_screen.Width();
	const int h = rc_screen.Height();
	if (w <= 0 || h <= 0)
		return;

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
	HGDIOBJ old = ::SelectObject(hdc_mem, hbmp);

	BOOL blt_ok = ::BitBlt(hdc_mem, 0, 0, w, h,
		hdc_screen, rc_screen.left, rc_screen.top, SRCCOPY | CAPTUREBLT);

	if (blt_ok && bits)
	{
		//BitBlt 결과는 BGRX. CF_DIB 호환을 위해 alpha 0xFF 채움.
		BYTE* p = static_cast<BYTE*>(bits);
		const int total = w * h;
		for (int i = 0; i < total; ++i)
			p[i * 4 + 3] = 0xFF;

		POINT pos = { rc_screen.left, rc_screen.top };
		send_image_to_clipboard_and_note(static_cast<const BYTE*>(bits), w, h, pos);
	}

	::SelectObject(hdc_mem, old);
	::DeleteObject(hbmp);
	::DeleteDC(hdc_mem);
	::ReleaseDC(NULL, hdc_screen);
}

void CSCDeskToolsDlg::OnToolCaptureFullscreen()
{
	//전체 가상 데스크톱(모든 모니터) 캡처. Common 의 get_monitor_rect(-1) 가 SM_*VIRTUALSCREEN 조합 대체.
	HideFloating hide(this);
	Wait(200);

	capture_screen_rect(get_monitor_rect(-1));
}

void CSCDeskToolsDlg::OnToolCaptureMonitor(UINT nID)
{
	//ID_TOOL_CAPTURE_MONITOR_FIRST + i 형태로 들어옴.
	const int idx = int(nID - ID_TOOL_CAPTURE_MONITOR_FIRST);
	enum_display_monitors();	//Common: g_monitors 갱신 (핫플러그/해상도 변경 대응)
	if (idx < 0 || idx >= int(g_monitors.size()))
		return;

	HideFloating hide(this);
	Wait(200);
	capture_screen_rect(g_monitors[idx].rMonitor);
}

void CSCDeskToolsDlg::OnToolCaptureWindow()
{
	HideFloating hide(this);

	//창들이 실제로 사라질 시간을 준다 (DWM 합성 1~2 프레임).
	Wait(200);

	CSCCaptureOverlayDlg dlg;
	if (!dlg.create(this))
	{
		AfxMessageBox(_T("캡처 오버레이 생성 실패"));
		return;
	}

	dlg.run_modal_loop(this);

	if (!dlg.is_picked())
		return;

	HWND hwnd = dlg.get_picked_hwnd();
	CRect rc_highlight = dlg.get_picked_rect_screen();
	CRect rc_virtual   = dlg.get_virtual_screen_rect();

	//1) PrintWindow 시도. 결과 DIB 는 클립보드용으로 살아있게 유지 (성공 시 클립보드 소유권 이전).
	CRect rc_window;
	::GetWindowRect(hwnd, &rc_window);

	//Win10+ 의 보이지 않는 drop shadow / resize 보더 영역을 빼고 실제 가시 rect 획득.
	//PrintWindow 는 GetWindowRect 크기로 캡처하므로 이후 가시 영역으로 crop 필요.
	//DwmGetWindowAttribute 결과도 좌·우·하 보더에 1px 의 anti-alias / 잔여 그림자가 남는 경우가 있어
	//DeflateRect(1, 0, 1, 1) 로 다듬어 줌 (top 은 정확하므로 그대로).
	CRect rc_visible = rc_window;
	{
		RECT rc_dwm = {};
		if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc_dwm, sizeof(rc_dwm))))
		{
			rc_visible = rc_dwm;
			rc_visible.DeflateRect(1, 0, 1, 1);
		}
	}

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

	//1.5) PrintWindow 성공 + DWM 가시 rect 가 GetWindowRect 보다 작으면 그 영역으로 crop.
	//     (Win10/11 의 보이지 않는 drop shadow / resize 보더 1~8px 가 캡처 결과에 포함되는 문제 해결.)
	if (pw_ok && hbmp_pw && rc_visible != rc_window)
	{
		const int dx = rc_visible.left - rc_window.left;
		const int dy = rc_visible.top  - rc_window.top;
		const int cw = rc_visible.Width();
		const int ch = rc_visible.Height();

		if (cw > 0 && ch > 0 && dx >= 0 && dy >= 0 && dx + cw <= pw_w && dy + ch <= pw_h)
		{
			HDC hdc_screen = ::GetDC(NULL);
			HDC hdc_src    = ::CreateCompatibleDC(hdc_screen);
			HDC hdc_dst    = ::CreateCompatibleDC(hdc_screen);

			BITMAPINFO bmi_c = {};
			bmi_c.bmiHeader.biSize        = sizeof(bmi_c.bmiHeader);
			bmi_c.bmiHeader.biWidth       = cw;
			bmi_c.bmiHeader.biHeight      = -ch;
			bmi_c.bmiHeader.biPlanes      = 1;
			bmi_c.bmiHeader.biBitCount    = 32;
			bmi_c.bmiHeader.biCompression = BI_RGB;

			void* dst_bits = nullptr;
			HBITMAP hbmp_crop = ::CreateDIBSection(hdc_dst, &bmi_c, DIB_RGB_COLORS, &dst_bits, NULL, 0);
			HGDIOBJ old_src = ::SelectObject(hdc_src, hbmp_pw);
			HGDIOBJ old_dst = ::SelectObject(hdc_dst, hbmp_crop);

			::BitBlt(hdc_dst, 0, 0, cw, ch, hdc_src, dx, dy, SRCCOPY);

			::SelectObject(hdc_src, old_src);
			::SelectObject(hdc_dst, old_dst);
			::DeleteDC(hdc_src);
			::DeleteDC(hdc_dst);
			::ReleaseDC(NULL, hdc_screen);

			//원본 PW 비트맵 교체. 이후 코드는 hbmp_pw / pw_w / pw_h 만 보면 되므로.
			::DeleteObject(hbmp_pw);
			hbmp_pw = hbmp_crop;
			pw_w = cw;
			pw_h = ch;
		}
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

	//3) 클립보드 + floating note — 헬퍼로 일원화. PrintWindow 성공이면 hbmp_pw, 아니면 hbmp_fallback.
	HBITMAP hbmp_for_clip = pw_ok ? hbmp_pw : hbmp_fallback;
	const int w_clip = pw_ok ? pw_w : rc_highlight.Width();
	const int h_clip = pw_ok ? pw_h : rc_highlight.Height();

	if (hbmp_for_clip && w_clip > 0 && h_clip > 0)
	{
		DIBSECTION ds = {};
		if (::GetObject(hbmp_for_clip, sizeof(ds), &ds) == sizeof(ds) && ds.dsBm.bmBits)
		{
			//note 위치 = 가시 rect 좌상단 (PrintWindow 성공 시 crop 된 영역의 시작점).
			//폴백 경로는 rc_highlight 가 이미 DWM 가시 rect 라 동일.
			POINT pos = { rc_visible.left, rc_visible.top };
			send_image_to_clipboard_and_note(
				static_cast<const BYTE*>(ds.dsBm.bmBits),
				w_clip, h_clip, pos);
		}
	}

	//우리가 소유한 HBITMAP 들은 클립보드 / 노트와 무관하게 모두 정리.
	if (hbmp_pw)       ::DeleteObject(hbmp_pw);
	if (hbmp_fallback) ::DeleteObject(hbmp_fallback);
}

void CSCDeskToolsDlg::OnToolCaptureRegion()
{
	HideFloating hide(this);
	Wait(200);

	CSCRegionCaptureDlg dlg;
	if (!dlg.create(this))
	{
		AfxMessageBox(_T("영역 캡처 오버레이 생성 실패"));
		return;
	}

	dlg.run_modal_loop(this);

	if (!dlg.is_picked())
		return;

	CRect rc_sel     = dlg.get_picked_rect_screen();
	CRect rc_virtual = dlg.get_virtual_screen_rect();
	HBITMAP hbmp_src = dlg.get_frozen_hbitmap();

	const int dst_w = rc_sel.Width();
	const int dst_h = rc_sel.Height();
	if (!hbmp_src || dst_w <= 0 || dst_h <= 0)
		return;

	//1) 프리즈 DIB 의 sub-region 을 새 32bpp top-down DIB section 으로 BitBlt.
	HDC hdc_screen = ::GetDC(NULL);
	HDC hdc_src    = ::CreateCompatibleDC(hdc_screen);
	HDC hdc_dst    = ::CreateCompatibleDC(hdc_screen);

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth       = dst_w;
	bmi.bmiHeader.biHeight      = -dst_h;	//top-down
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* dst_bits = nullptr;
	HBITMAP hbmp_dst = ::CreateDIBSection(hdc_dst, &bmi, DIB_RGB_COLORS, &dst_bits, NULL, 0);
	HGDIOBJ old_src = ::SelectObject(hdc_src, hbmp_src);
	HGDIOBJ old_dst = ::SelectObject(hdc_dst, hbmp_dst);

	::BitBlt(hdc_dst, 0, 0, dst_w, dst_h,
		hdc_src,
		rc_sel.left - rc_virtual.left,
		rc_sel.top  - rc_virtual.top,
		SRCCOPY);

	::SelectObject(hdc_src, old_src);
	::SelectObject(hdc_dst, old_dst);
	::DeleteDC(hdc_src);
	::DeleteDC(hdc_dst);
	::ReleaseDC(NULL, hdc_screen);

	//2) 클립보드 + floating note — 헬퍼로 일원화.
	if (hbmp_dst && dst_bits)
	{
		POINT pos = { rc_sel.left, rc_sel.top };
		send_image_to_clipboard_and_note(static_cast<const BYTE*>(dst_bits), dst_w, dst_h, pos);
	}

	if (hbmp_dst)
		::DeleteObject(hbmp_dst);
}

void CSCDeskToolsDlg::OnToolProtractor()
{
	HideFloating hide(this);
	Wait(200);

	CSCProtractorDlg dlg;
	if (!dlg.create(this))
	{
		AfxMessageBox(_T("각도기 오버레이 생성 실패"));
		return;
	}

	dlg.run_modal_loop(this);
}

void CSCDeskToolsDlg::OnToolRuler()
{
	HideFloating hide(this);
	Wait(200);

	CSCRulerDlg dlg;
	if (!dlg.create(this))
	{
		AfxMessageBox(_T("줄자 오버레이 생성 실패"));
		return;
	}

	dlg.run_modal_loop(this);
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

void CSCDeskToolsDlg::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	CDialogEx::OnWindowPosChanged(lpwndpos);

	// TODO: 여기에 메시지 처리기 코드를 추가합니다.
	SaveWindowPosition(&theApp, this);
}
