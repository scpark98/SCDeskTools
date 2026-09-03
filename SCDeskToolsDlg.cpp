
// SCDeskToolsDlg.cpp: 구현 파일
//

#include "pch.h"
#include "framework.h"
#include "SCDeskTools.h"
#include "SCDeskToolsDlg.h"
#include "afxdialogex.h"

#include <vector>
#include <algorithm>

#include "Common/Functions.h"
#include "Common/CDialog/CSCColorPicker/SCDropperDlg.h"
#include "SCCaptureOverlayDlg.h"
#include "SCCapturedNoteDlg.h"
#include "SCRegionCaptureDlg.h"
#include "SCFreehandCaptureDlg.h"
#include "SCProtractorDlg.h"
#include "SCRulerDlg.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

#include <shellscalingapi.h>
#pragma comment(lib, "shcore.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//===== 툴 레지스트리 =====
//툴을 추가하려면 (1) Resource.h 에 ID_TOOL_* 정의, (2) 핸들러 함수 + ON_COMMAND 추가,
//(3) tools 에 한 줄 추가, (4) 필요하면 default_favorites 에 ID 넣어 메인 창에 노출.
//build_buttons / show_tools_popup_menu 는 자동으로 신규 툴을 반영함.
namespace
{
	enum ToolCat
	{
		cat_capture,
		cat_color,
		cat_measure,
	};

	struct ToolItem
	{
		UINT		id;
		LPCTSTR		button_text;	//메인 창 버튼 라벨 (액셀러레이터 없음)
		LPCTSTR		menu_text;		//트레이 메뉴 라벨 (&X 액셀러레이터 포함)
		ToolCat		cat;
	};

	//menu_text 에는 더 이상 `(&X)` 액셀러레이터를 두지 않는다.
	//툴 항목은 모두 글로벌 단축키(hotkeys) 가 있으므로 메뉴 표시 시점에 `\tAlt+Shift+X` 형태로 자동 부착.
	const ToolItem tools[] =
	{
		//Capture
		{ ID_TOOL_CAPTURE_FULLSCREEN,	_T("전체 화면 캡처"),			_T("전체 화면 캡처"),			cat_capture },
		{ ID_TOOL_CAPTURE_WINDOW,		_T("창 캡처"),					_T("창 캡처"),					cat_capture },
		{ ID_TOOL_CAPTURE_REGION,		_T("영역 캡처"),					_T("영역 캡처"),					cat_capture },
		{ ID_TOOL_CAPTURE_FREEHAND,		_T("자유 영역 캡처"),			_T("자유 영역 캡처"),			cat_capture },
		{ ID_TOOL_PASTE_CLIPBOARD,		_T("클립보드 이미지 띠우기"),		_T("클립보드 이미지 띠우기"),		cat_capture },
		//Color
		{ ID_TOOL_COLOR_PICKER,			_T("컬러 피커"),					_T("컬러 피커"),					cat_color },
		{ ID_TOOL_DROPPER,				_T("화면 돋보기"),				_T("화면 돋보기"),				cat_color },
		//Measure
		{ ID_TOOL_PROTRACTOR,			_T("각도기"),					_T("각도기"),					cat_measure },
		{ ID_TOOL_RULER,				_T("줄자"),						_T("줄자"),						cat_measure },
	};

	struct CategoryInfo
	{
		ToolCat		cat;
		LPCTSTR		title;
	};

	const CategoryInfo categories[] =
	{
		{ cat_capture,	_T("캡처(&P)") },
		{ cat_color,	_T("색상(&C)") },
		{ cat_measure,	_T("측정(&M)") },
	};

	//메인 창 기본 즐겨찾기. 사용자가 설정창에서 편집 시 레지스트리로 옮길 예정.
	const UINT default_favorites[] =
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
		for (const ToolItem& t : tools)
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
		UINT		tool_id;		//ID_TOOL_* ? WM_COMMAND 으로 dispatch
		UINT		modifiers;		//MOD_ALT | MOD_SHIFT | ... (MOD_NOREPEAT 권장)
		UINT		vkey;
		LPCTSTR		description;	//등록 실패 알림 / 향후 설정 다이얼로그용
	};

	//모니터 캡처 단축키는 모니터 개수에 따라 동적 ? hotkeys 와 ID 충돌 피하려고 100 base 사용.
	//Alt+Shift+'1'..'9' 까지 최대 9 개 모니터 지원.
	const int monitor_hotkey_id_base = 100;
	const int monitor_hotkey_max_count = 9;

	const HotkeyItem hotkeys[] =
	{
		{ 1,	ID_TOOL_CAPTURE_WINDOW,		MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,	'S',	_T("창 캡처 (Alt+Shift+S)") },
		{ 2,	ID_TOOL_CAPTURE_FULLSCREEN,	MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,	'F',	_T("전체 화면 캡처 (Alt+Shift+F)") },
		{ 3,	ID_TOOL_CAPTURE_REGION,		MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,	'R',	_T("영역 캡처 (Alt+Shift+R)") },
		{ 9,	ID_TOOL_CAPTURE_FREEHAND,	MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,	'H',	_T("자유 영역 캡처 (Alt+Shift+H)") },
		{ 4,	ID_TOOL_PASTE_CLIPBOARD,	MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,	'V',	_T("클립보드 이미지 띠우기 (Alt+Shift+V)") },
		{ 5,	ID_TOOL_COLOR_PICKER,		MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,	'C',	_T("컬러 피커 (Alt+Shift+C)") },
		{ 6,	ID_TOOL_DROPPER,			MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,	'M',	_T("화면 돋보기 (Alt+Shift+M)") },
		{ 7,	ID_TOOL_PROTRACTOR,			MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,	'P',	_T("각도기 (Alt+Shift+P)") },
		{ 8,	ID_TOOL_RULER,				MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,	'L',	_T("줄자 (Alt+Shift+L)") },
	};

	UINT find_tool_id_by_hotkey_id(int hotkey_id)
	{
		for (const HotkeyItem& h : hotkeys)
		{
			if (h.id == hotkey_id)
				return h.tool_id;
		}
		return 0;
	}

	const HotkeyItem* find_hotkey_for_tool(UINT tool_id)
	{
		for (const HotkeyItem& h : hotkeys)
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
		if (modifiers & MOD_ALT)	s += _T("Alt+");
		if (modifiers & MOD_SHIFT)	s += _T("Shift+");
		if (modifiers & MOD_WIN)	s += _T("Win+");

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
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 지원입니다.

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
	ON_COMMAND(ID_TOOL_CAPTURE_FREEHAND, &CSCDeskToolsDlg::OnToolCaptureFreehand)
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
	ON_BN_CLICKED(id_check_clipboard_only, &CSCDeskToolsDlg::OnBnClickedClipboardOnly)
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

	CString caption;
	caption.Format(_T("SCDeskTools (ver %s)"), get_file_property());
	SetWindowText(caption);

	//기본 즐겨찾기 채우기 (추후: 레지스트리에서 읽기 → 설정창 편집 가능).
	m_favorites.assign(std::begin(default_favorites), std::end(default_favorites));

	//클립보드 전용 옵션 — 레지스트리에서 복원 (default 0).
	m_clipboard_only = AfxGetApp()->GetProfileInt(_T("settings"), _T("capture_clipboard_only"), 0) != 0;

	//.rc 의 디폴트(480x325) 보다 작게 ? 8 즐겨찾기 + 개발 종료 버튼 + 체크박스가 깔끔히 들어가는 컴팩트 크기.
	//build_buttons 가 GetClientRect 로 배치하므로 반드시 그 전에 호출.
	{
		//높이 = top_margin(14) + 4행 * btn_h(50) + 3 gap * 10 + checkbox_gap(10) + checkbox_h(20) + bottom_margin(14) = 288.
		const int target_client_w = 400;
		const int target_client_h = 288;
		CRect target(0, 0, target_client_w, target_client_h);
		::AdjustWindowRectEx(&target, GetStyle(), FALSE, GetExStyle());
		SetWindowPos(NULL, 0, 0, target.Width(), target.Height(),
			SWP_NOMOVE | SWP_NOZORDER);
	}

	build_buttons();
	build_exit_button();
	build_clipboard_only_checkbox();

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

	//트레이 상주 앱 ? 시작 시 메인 창 숨김. 트레이 더블클릭/단축키/우클릭 메뉴로 표시.
	//OnInitDialog 안에서 ShowWindow(SW_HIDE) 호출하면 다이얼로그 매니저가 다시 보이게 만들 수 있어
	//PostMessage 로 첫 페인트 직후 시점에 숨김 처리. (브리프 플래시 가능하지만 허용)
	PostMessage(Message_HideOnStartup);

	return TRUE;	// 포커스를 컨트롤에 설정하지 않으면 TRUE를 반환합니다.
}

void CSCDeskToolsDlg::build_buttons()
{
	//m_favorites 의 ID 들을 순서대로 텍스트 버튼으로 배치. ID 는 ON_COMMAND 가 자동 처리.
	//추후 아이콘 포함 버튼 (CSCButton 등) 으로 교체 가능.
	m_buttons_favorite.clear();

	CRect rc_client;
	GetClientRect(rc_client);

	const int margin = 14;
	const int gap_x	= 10;
	const int gap_y	= 10;
	const int cols	= 2;
	const int btn_w	= (rc_client.Width() - margin * 2 - gap_x * (cols - 1)) / cols;
	//두 줄 캡션 (기능명 / 글로벌 단축키) 수용 위해 한 줄짜리 36 → 50.
	const int btn_h	= 50;

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
		CString hotkey	= hotkey_text_for_tool(tool->id);
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
	const int btn_w	= 80;
	const int btn_h	= 32;

	CRect rc_btn(
		rc_client.right	- margin - btn_w,
		rc_client.bottom - margin - btn_h,
		rc_client.right	- margin,
		rc_client.bottom - margin);

	m_button_exit.Create(_T("종료"),
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
		rc_btn, this, ID_SC_EXIT);
	m_button_exit.SetFont(GetFont());
}

void CSCDeskToolsDlg::build_clipboard_only_checkbox()
{
	CRect rc_client;
	GetClientRect(rc_client);

	const int margin = 14;
	const int check_h = 20;

	CRect rc_check(
		rc_client.left	+ margin,
		rc_client.bottom - margin - check_h,
		rc_client.right	- margin,
		rc_client.bottom - margin);

	m_check_clipboard_only.Create(_T("캡처 시 클립보드로만 저장(플로팅 창으로 띠우지 않음)"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
		rc_check, this, id_check_clipboard_only);
	m_check_clipboard_only.SetFont(GetFont());
	m_check_clipboard_only.SetCheck(m_clipboard_only ? BST_CHECKED : BST_UNCHECKED);
}

void CSCDeskToolsDlg::OnBnClickedClipboardOnly()
{
	m_clipboard_only = (m_check_clipboard_only.GetCheck() == BST_CHECKED);
	AfxGetApp()->WriteProfileInt(_T("settings"), _T("capture_clipboard_only"), m_clipboard_only ? 1 : 0);
}

void CSCDeskToolsDlg::show_tools_popup_menu(CPoint pt_screen)
{
	//트레이 우클릭 시 표시할 팝업. 카테고리별 서브메뉴 + 메인 창 표시/숨김 + 정보/종료.
	//툴 항목은 tools / categories 자동 반영 → 새 툴 추가 시 메뉴 코드 수정 불필요.
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, ID_APP_SHOW_HIDE,
		IsWindowVisible() ? _T("숨기기(&H)") : _T("표시(&S)"));
	menu.AppendMenu(MF_SEPARATOR);

	for (const CategoryInfo& ci : categories)
	{
		CMenu sub;
		sub.CreatePopupMenu();
		for (const ToolItem& t : tools)
		{
			if (t.cat != ci.cat)
				continue;
			UINT flags = MF_STRING;
			if (t.id == ID_TOOL_PASTE_CLIPBOARD && !clipboard_has_image())
				flags |= MF_GRAYED;

			//"기능명\tAlt+Shift+X" ? Tab 이후 부분은 OS 가 메뉴 우측에 정렬해 표시.
			//단축키 미등록 항목은 Tab 자체를 생략.
			CString item_text = t.menu_text;
			CString hotkey	= hotkey_text_for_tool(t.id);
			if (!hotkey.IsEmpty())
			{
				item_text += _T("\t");
				item_text += hotkey;
			}
			sub.AppendMenu(flags, t.id, item_text);

			//전체 화면 캡처 바로 다음에 모니터별 캡처 항목들을 같은 서브메뉴에 펼쳐서 추가 (모니터 2개 이상일 때만).
			if (t.id == ID_TOOL_CAPTURE_FULLSCREEN)
			{
				enum_display_monitors();	//Common: g_monitors 갱신
				if (g_monitors.size() >= 2)
				{
					const size_t max_n = std::min<size_t>(g_monitors.size(),
						ID_TOOL_CAPTURE_MONITOR_LAST - ID_TOOL_CAPTURE_MONITOR_FIRST + 1);
					for (size_t i = 0; i < max_n; ++i)
					{
						CString s;
						s.Format(_T("%d번 모니터 캡처"), int(i + 1));
						if (i < monitor_hotkey_max_count)
							s.AppendFormat(_T("\tAlt+Shift+%d"), int(i + 1));
						sub.AppendMenu(MF_STRING, ID_TOOL_CAPTURE_MONITOR_FIRST + UINT(i), s);
					}
				}
			}
		}
		menu.AppendMenu(MF_POPUP, (UINT_PTR)sub.Detach(), ci.title);
	}

	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_HELP_ABOUT, _T("정보(&A)..."));
	menu.AppendMenu(MF_STRING, ID_SC_EXIT,	_T("종료(&X)"));

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
	//다른 앱이 이미 잡고 있는 조합은 등록 실패 ? 실패 항목만 모아 트레이 풍선으로 알림.
	//RegisterHotKey TRUE 인데도 WM_HOTKEY 가 안 오면 다른 앱의 WH_KEYBOARD_LL hook 가로채기.
	//(NVIDIA GeForce Experience, Xbox Game Bar, Discord, OBS 등이 흔한 범인.)
	CString failed;
	for (const HotkeyItem& h : hotkeys)
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
	for (const HotkeyItem& h : hotkeys)
		::UnregisterHotKey(m_hWnd, h.id);
}

void CSCDeskToolsDlg::register_monitor_hotkeys()
{
	//기존 동적 등록분 해제 후 g_monitors 갱신 → 현재 개수만큼 재등록.
	//단일 모니터일 때는 등록 생략 (전체 화면 캡처와 동일하므로 단축키 낭비).
	unregister_monitor_hotkeys();

	enum_display_monitors();	//Common ? g_monitors 채움
	if (g_monitors.size() < 2)
		return;

	const int n = static_cast<int>(std::min<size_t>(g_monitors.size(), monitor_hotkey_max_count));
	for (int i = 0; i < n; ++i)
	{
		const UINT vkey = '1' + UINT(i);	//Alt+Shift+'1'..'9'
		const int	id	= monitor_hotkey_id_base + i;
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
		::UnregisterHotKey(m_hWnd, monitor_hotkey_id_base + i);
	m_registered_monitor_count = 0;
}

LRESULT CSCDeskToolsDlg::on_hotkey(WPARAM wParam, LPARAM /*lParam*/)
{
	//기존 ON_COMMAND 핸들러로 위임 ? 메뉴 / 버튼 / 단축키 단일 진입점 유지.
	const int hotkey_id = static_cast<int>(wParam);

	//1) 정적 툴 단축키 (hotkeys, id 1..8)
	UINT tool_id = find_tool_id_by_hotkey_id(hotkey_id);
	if (tool_id != 0)
	{
		TRACE(_T("[hotkey] WM_HOTKEY id=%d -> tool_id=0x%X\n"), hotkey_id, tool_id);
		SendMessage(WM_COMMAND, MAKEWPARAM(tool_id, 0), 0);
		return 0;
	}

	//2) 동적 모니터 단축키 (monitor_hotkey_id_base + i)
	if (hotkey_id >= monitor_hotkey_id_base &&
		hotkey_id <	monitor_hotkey_id_base + monitor_hotkey_max_count)
	{
		const int monitor_idx = hotkey_id - monitor_hotkey_id_base;
		const UINT mon_id = ID_TOOL_CAPTURE_MONITOR_FIRST + UINT(monitor_idx);
		TRACE(_T("[hotkey] WM_HOTKEY monitor idx=%d -> 0x%X\n"), monitor_idx, mon_id);
		SendMessage(WM_COMMAND, MAKEWPARAM(mon_id, 0), 0);
		return 0;
	}

	return 0;
}

LRESULT CSCDeskToolsDlg::on_display_change(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	//모니터 핫플러그·해상도 변경 ? 모니터 캐시 갱신 + 단축키 재등록.
	//정적 툴 단축키(hotkeys) 는 디스플레이 무관이라 재등록 불필요.
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
		point.x = rc.left + rc.Width()	/ 2;
		point.y = rc.top	+ rc.Height() / 2;
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
	/*
	//modeless 피커가 색상 변경을 알릴 때마다 도착. 단계 1 에서는 타이틀바에 hex 만 반영.
	auto msg = reinterpret_cast<CSCColorPickerMessage*>(wParam);
	m_cr_selected = msg->cr_selected;

	CString title;
	title.Format(_T("SCDeskTools - #%02X%02X%02X"),
		m_cr_selected.GetR(), m_cr_selected.GetG(), m_cr_selected.GetB());
	SetWindowText(title);
	*/
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
}

//본 파일 하단 정의를 앞쪽 사용처에서 참조 가능하게 forward declaration.
static HGLOBAL encode_bgra_to_png_hglobal(const BYTE* bgra_top_down, int w, int h, int dpi_x, int dpi_y);
static bool decode_png_hglobal_to_bgra(HGLOBAL hg, std::vector<BYTE>& out_bgra_top_down, int& out_w, int& out_h);

void CSCDeskToolsDlg::send_image_to_clipboard_and_note(const BYTE* bgra_top_down, int w, int h, POINT note_pos)
{
	//top-down BGRA 픽셀을 받아 (1) CF_DIBV5 + CF_DIB 로 클립보드 복사, (2) floating note 띠움.
	//CF_DIBV5 가 alpha 채널 인식 가능 → PowerPoint 등 modern 앱이 라운드 코너 투명 처리.
	//CF_DIB 는 legacy fallback (alpha 무시).
	if (!bgra_top_down || w <= 0 || h <= 0)
		return;

	const int stride = w * 4;
	const DWORD pixel_size = static_cast<DWORD>(stride) * h;

	//시스템 DPI 획득 → PNG pHYs / DIB biXPelsPerMeter 에 기록.
	//PowerPoint 등은 (픽셀수 ÷ DPI) 로 슬라이드 인치 크기를 결정. 메타데이터 누락 시 96 으로 가정해
	//150%/200% 스케일 환경에서 1.5/2 배 확대된 채로 붙음. ppm = dpi × 39.3701 (m → inch).
	HDC hdc_screen = ::GetDC(NULL);
	const int dpi_x = ::GetDeviceCaps(hdc_screen, LOGPIXELSX);
	const int dpi_y = ::GetDeviceCaps(hdc_screen, LOGPIXELSY);
	::ReleaseDC(NULL, hdc_screen);
	const LONG ppm_x = static_cast<LONG>(double(dpi_x) * 39.3701 + 0.5);
	const LONG ppm_y = static_cast<LONG>(double(dpi_y) * 39.3701 + 0.5);

	auto fill_bottom_up = [&](BYTE* dst)
	{
		for (int y = 0; y < h; ++y)
			memcpy(dst + (h - 1 - y) * stride, bgra_top_down + y * stride, stride);
	};

	HGLOBAL hg_v5 = ::GlobalAlloc(GHND, sizeof(BITMAPV5HEADER) + pixel_size);
	if (hg_v5)
	{
		BYTE* mem = static_cast<BYTE*>(::GlobalLock(hg_v5));
		BITMAPV5HEADER* bv5 = reinterpret_cast<BITMAPV5HEADER*>(mem);
		bv5->bV5Size			= sizeof(BITMAPV5HEADER);
		bv5->bV5Width			= w;
		bv5->bV5Height			= h;
		bv5->bV5Planes			= 1;
		bv5->bV5BitCount		= 32;
		bv5->bV5Compression		= BI_BITFIELDS;
		bv5->bV5SizeImage		= pixel_size;
		bv5->bV5XPelsPerMeter	= ppm_x;
		bv5->bV5YPelsPerMeter	= ppm_y;
		bv5->bV5RedMask			= 0x00FF0000;
		bv5->bV5GreenMask		= 0x0000FF00;
		bv5->bV5BlueMask		= 0x000000FF;
		bv5->bV5AlphaMask		= 0xFF000000;
		bv5->bV5CSType			= LCS_WINDOWS_COLOR_SPACE;
		bv5->bV5Intent			= LCS_GM_GRAPHICS;
		fill_bottom_up(mem + sizeof(BITMAPV5HEADER));
		::GlobalUnlock(hg_v5);
	}

	HGLOBAL hg_dib = ::GlobalAlloc(GHND, sizeof(BITMAPINFOHEADER) + pixel_size);
	if (hg_dib)
	{
		BYTE* mem = static_cast<BYTE*>(::GlobalLock(hg_dib));
		BITMAPINFOHEADER* bih = reinterpret_cast<BITMAPINFOHEADER*>(mem);
		bih->biSize			= sizeof(BITMAPINFOHEADER);
		bih->biWidth		= w;
		bih->biHeight		= h;
		bih->biPlanes		= 1;
		bih->biBitCount		= 32;
		bih->biCompression	= BI_RGB;
		bih->biSizeImage	= pixel_size;
		bih->biXPelsPerMeter = ppm_x;
		bih->biYPelsPerMeter = ppm_y;
		fill_bottom_up(mem + sizeof(BITMAPINFOHEADER));
		::GlobalUnlock(hg_dib);
	}

	//"PNG" 등록 포맷 — PowerPoint / Word 등이 alpha 채널 인식하는 가장 신뢰할 만한 경로.
	HGLOBAL hg_png = encode_bgra_to_png_hglobal(bgra_top_down, w, h, dpi_x, dpi_y);
	const UINT cf_png = ::RegisterClipboardFormat(_T("PNG"));

	if (::OpenClipboard(m_hWnd))
	{
		::EmptyClipboard();
		//순서: PNG → DIBV5 → DIB. 받는 앱은 자기가 지원하는 첫 포맷을 골라 alpha-aware 우선.
		HANDLE r_pn = (hg_png && cf_png) ? ::SetClipboardData(cf_png,  hg_png) : NULL;
		HANDLE r_v5 = hg_v5  ? ::SetClipboardData(CF_DIBV5, hg_v5)  : NULL;
		HANDLE r_db = hg_dib ? ::SetClipboardData(CF_DIB,   hg_dib) : NULL;
		::CloseClipboard();
		if (hg_png && r_pn == NULL) ::GlobalFree(hg_png);
		if (hg_v5  && r_v5 == NULL) ::GlobalFree(hg_v5);
		if (hg_dib && r_db == NULL) ::GlobalFree(hg_dib);
	}
	else
	{
		if (hg_png) ::GlobalFree(hg_png);
		if (hg_v5)  ::GlobalFree(hg_v5);
		if (hg_dib) ::GlobalFree(hg_dib);
	}

	if (!m_clipboard_only)
		CSCCapturedNoteDlg::spawn(bgra_top_down, w, h, &note_pos);
}

CSCDeskToolsDlg::HideFloating::HideFloating(CSCDeskToolsDlg* d)
	: dlg(d)
{
	main_was_visible	= (d->IsWindowVisible() && !d->IsIconic()) ? true : false;
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

//BGRA top-down 픽셀을 PNG 로 인코딩해 HGLOBAL 반환. 클립보드 "PNG" 포맷용.
//PowerPoint / Word 등 modern 앱은 등록 포맷 "PNG" 를 우선해서 alpha 보존.
//성공 시 호출자가 SetClipboardData 로 ownership 이전 (실패 시 GlobalFree).
static HGLOBAL encode_bgra_to_png_hglobal(const BYTE* bgra_top_down, int w, int h, int dpi_x, int dpi_y)
{
	HGLOBAL hg_out = NULL;
	IWICImagingFactory* pFactory = NULL;
	IStream* pStream = NULL;
	IWICBitmapEncoder* pEncoder = NULL;
	IWICBitmapFrameEncode* pFrame = NULL;

	HRESULT hr = ::CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pFactory));
	if (FAILED(hr) || !pFactory)
		goto cleanup;

	hr = ::CreateStreamOnHGlobal(NULL, TRUE, &pStream);
	if (FAILED(hr) || !pStream)
		goto cleanup;

	hr = pFactory->CreateEncoder(GUID_ContainerFormatPng, NULL, &pEncoder);
	if (FAILED(hr) || !pEncoder)
		goto cleanup;

	hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
	if (FAILED(hr))
		goto cleanup;

	hr = pEncoder->CreateNewFrame(&pFrame, NULL);
	if (FAILED(hr) || !pFrame)
		goto cleanup;

	hr = pFrame->Initialize(NULL);
	if (FAILED(hr))
		goto cleanup;

	hr = pFrame->SetSize(w, h);
	if (FAILED(hr))
		goto cleanup;
	//PNG pHYs 청크 기록. PowerPoint 가 (픽셀수 ÷ DPI) 인치로 슬라이드 크기 계산.
	//SetResolution 은 Initialize 이후, Commit 이전 어디서든 호출 가능.
	hr = pFrame->SetResolution(double(dpi_x), double(dpi_y));
	if (FAILED(hr))
		goto cleanup;
	{
		WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
		hr = pFrame->SetPixelFormat(&pf);
		if (FAILED(hr))
			goto cleanup;
	}
	{
		const UINT stride = static_cast<UINT>(w) * 4;
		const UINT cb = stride * static_cast<UINT>(h);
		hr = pFrame->WritePixels(static_cast<UINT>(h), stride, cb, const_cast<BYTE*>(bgra_top_down));
		if (FAILED(hr))
			goto cleanup;
	}
	hr = pFrame->Commit();
	if (FAILED(hr))
		goto cleanup;
	hr = pEncoder->Commit();
	if (FAILED(hr))
		goto cleanup;

	{
		STATSTG stat = {};
		hr = pStream->Stat(&stat, STATFLAG_NONAME);
		if (FAILED(hr))
			goto cleanup;
		const SIZE_T size = static_cast<SIZE_T>(stat.cbSize.QuadPart);
		hg_out = ::GlobalAlloc(GHND, size);
		if (!hg_out)
			goto cleanup;

		LARGE_INTEGER zero = {};
		pStream->Seek(zero, STREAM_SEEK_SET, NULL);
		BYTE* mem = static_cast<BYTE*>(::GlobalLock(hg_out));
		ULONG read = 0;
		pStream->Read(mem, static_cast<ULONG>(size), &read);
		::GlobalUnlock(hg_out);
	}

cleanup:
	if (pFrame)		pFrame->Release();
	if (pEncoder)	pEncoder->Release();
	if (pStream)	pStream->Release();
	if (pFactory)	pFactory->Release();
	return hg_out;
}

//클립보드에서 가져온 PNG HGLOBAL → 32bpp BGRA top-down 픽셀로 디코드.
//성공 시 out_bgra_top_down 에 w*h*4 바이트 채움. alpha 채널 보존.
static bool decode_png_hglobal_to_bgra(HGLOBAL hg, std::vector<BYTE>& out_bgra_top_down, int& out_w, int& out_h)
{
	if (!hg)
		return false;

	bool ok = false;
	IWICImagingFactory* pFactory = NULL;
	IStream* pStream = NULL;
	IWICBitmapDecoder* pDecoder = NULL;
	IWICBitmapFrameDecode* pFrame = NULL;
	IWICFormatConverter* pConverter = NULL;

	const SIZE_T size = ::GlobalSize(hg);
	if (size == 0)
		return false;
	BYTE* mem = static_cast<BYTE*>(::GlobalLock(hg));
	if (!mem)
		return false;

	HRESULT hr = ::CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&pFactory));
	if (FAILED(hr) || !pFactory)
		goto cleanup;

	hr = ::CreateStreamOnHGlobal(NULL, TRUE, &pStream);
	if (FAILED(hr) || !pStream)
		goto cleanup;
	{
		ULONG written = 0;
		pStream->Write(mem, static_cast<ULONG>(size), &written);
		LARGE_INTEGER zero = {};
		pStream->Seek(zero, STREAM_SEEK_SET, NULL);
	}

	hr = pFactory->CreateDecoderFromStream(pStream, NULL, WICDecodeMetadataCacheOnDemand, &pDecoder);
	if (FAILED(hr) || !pDecoder)
		goto cleanup;

	hr = pDecoder->GetFrame(0, &pFrame);
	if (FAILED(hr) || !pFrame)
		goto cleanup;

	hr = pFactory->CreateFormatConverter(&pConverter);
	if (FAILED(hr) || !pConverter)
		goto cleanup;

	hr = pConverter->Initialize(pFrame, GUID_WICPixelFormat32bppBGRA,
		WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
	if (FAILED(hr))
		goto cleanup;
	{
		UINT w = 0, h = 0;
		hr = pConverter->GetSize(&w, &h);
		if (FAILED(hr) || w == 0 || h == 0)
			goto cleanup;

		const UINT stride = w * 4;
		const UINT cb = stride * h;
		out_bgra_top_down.resize(cb);
		hr = pConverter->CopyPixels(NULL, stride, cb, out_bgra_top_down.data());
		if (FAILED(hr))
			goto cleanup;

		out_w = static_cast<int>(w);
		out_h = static_cast<int>(h);
		ok = true;
	}

cleanup:
	if (pConverter)	pConverter->Release();
	if (pFrame)		pFrame->Release();
	if (pDecoder)	pDecoder->Release();
	if (pStream)	pStream->Release();
	if (pFactory)	pFactory->Release();
	::GlobalUnlock(hg);
	return ok;
}

//20260727 by claude. 대상 창이 올라가 있는 모니터의 DPI.
//GetDpiForWindow 가 아니라 모니터 DPI 를 쓰는 이유: 전자는 대상 창의 DPI 인식 수준을 따라가
//DPI-unaware 앱이면 175% 모니터 위에서도 96 을 돌려준다. DWM 이 그리는 프레임(테두리·라운드 호)은
//대상 앱의 인식 수준과 무관하게 모니터 물리 픽셀 기준이다.
static UINT monitor_dpi_for_window(HWND hwnd)
{
	UINT dpi_x = 96;
	UINT dpi_y = 96;
	HMONITOR monitor = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	if (FAILED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)))
		return 96;
	return dpi_x;
}

//Win11+ 라운드 코너 반경 추정. Win10/older 또는 명시적 DONOTROUND 면 0.
//DEFAULT 는 "DWM 이 스타일 보고 결정" — 캡션/씩프레임 없는 borderless·popup 창엔
//DWM 이 라운드를 적용 안 하므로 그 경우 0 반환. 명시적 ROUND/ROUNDSMALL 은 스타일 무관.
static int probe_window_corner_radius(HWND hwnd)
{
	const DWORD kAttrCornerPref = 33;	//DWMWA_WINDOW_CORNER_PREFERENCE (Win11). Win10 에서는 E_INVALIDARG.
	int pref = 0;
	if (FAILED(::DwmGetWindowAttribute(hwnd, kAttrCornerPref, &pref, sizeof(pref))))
		return 0;
	const int kDefault = 0, kDoNotRound = 1, kRound = 2, kRoundSmall = 3;
	if (pref == kDoNotRound)
		return 0;
	if (pref == kDefault)
	{
		const LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
		if ((style & (WS_CAPTION | WS_THICKFRAME)) == 0)
			return 0;	//borderless/popup → DWM 라운드 없음
	}
	//20260727 by claude. Win11 라운드 반경은 논리 8px(작은 코너 4px) 이고, 이 앱은 PMv2 라
	//캡처 좌표가 물리 픽셀이다. 스케일하지 않으면 175% 에서 실제 호보다 작게 잘려 코너가 어긋난다.
	const UINT dpi = monitor_dpi_for_window(hwnd);

	if (pref == kRoundSmall)
		return int(4 * dpi / 96);
	return int(8 * dpi / 96);	//ROUND 또는 DEFAULT(캡션 있는 표준 창)
}

//32bpp BGRA top-down 픽셀의 4모서리에 라운드 마스크 적용 (in-place, antialiased).
//- coverage = (radius + 0.5) - dist : 호 안쪽=1, 바깥=0, 호 위 1px 폭에서 부분값
//- cov >= 1 (호 안쪽)             : 원본 그대로
//- cov <= 0 (호 바깥)             : (0,0,0,0) — alpha 무시 뷰어에서도 마스킹 시각 확인 가능
//- 0 < cov < 1 (edge band)         : straight alpha 로 RGB 유지 + alpha *= cov
//                                    (PNG 디코더가 straight 로 해석해 부드러운 AA 합성)
static void apply_rounded_corner_alpha(BYTE* px, int w, int h, int radius)
{
	if (!px || radius <= 0 || w <= 2 * radius || h <= 2 * radius)
		return;
	const float r = float(radius);
	auto process = [&](int x0, int y0, int cx, int cy)
	{
		for (int y = y0; y < y0 + radius; ++y)
		{
			const float dy = float(y) + 0.5f - float(cy);
			BYTE* row = px + size_t(y) * size_t(w) * 4;
			for (int x = x0; x < x0 + radius; ++x)
			{
				const float dx = float(x) + 0.5f - float(cx);
				const float dist = sqrtf(dx * dx + dy * dy);
				const float cov = r + 0.5f - dist;
				BYTE* p = row + x * 4;
				if (cov >= 1.0f)
					continue;
				if (cov <= 0.0f)
				{
					p[0] = 0;
					p[1] = 0;
					p[2] = 0;
					p[3] = 0;
				}
				else
				{
					p[3] = static_cast<BYTE>(float(p[3]) * cov + 0.5f);
				}
			}
		}
	};
	process(0,            0,            radius,     radius);		//TL
	process(w - radius,   0,            w - radius, radius);		//TR
	process(0,            h - radius,   radius,     h - radius);	//BL
	process(w - radius,   h - radius,   w - radius, h - radius);	//BR
}

//20260727 by claude. 링(사각 테두리 한 겹) 의 픽셀 색을 모은다. 라운드 코너 구간은 호 AA 값이
//섞여 판정을 흐리므로 corner_skip 만큼 양 끝을 제외.
static void collect_ring_colors(const BYTE* px, int w, int h, int ring, int corner_skip, std::vector<DWORD>& out)
{
	out.clear();

	const int x0 = ring;
	const int x1 = w - 1 - ring;
	const int y0 = ring;
	const int y1 = h - 1 - ring;
	if (x1 - x0 < 2 * corner_skip || y1 - y0 < 2 * corner_skip)
		return;

	auto color_at = [&](int x, int y) -> DWORD
	{
		const BYTE* p = px + (size_t(y) * size_t(w) + size_t(x)) * 4;
		return DWORD(p[0]) | (DWORD(p[1]) << 8) | (DWORD(p[2]) << 16);
	};

	for (int x = x0 + corner_skip; x <= x1 - corner_skip; ++x)
	{
		out.push_back(color_at(x, y0));
		out.push_back(color_at(x, y1));
	}
	for (int y = y0 + corner_skip; y <= y1 - corner_skip; ++y)
	{
		out.push_back(color_at(x0, y));
		out.push_back(color_at(x1, y));
	}
}

//20260727 by claude. 링 픽셀과 "그 위치에서 창 바깥 배경" 을 짝지어 모은다.
//바깥 배경은 프리즈 DIB(가상 데스크톱 전체) 에서 창 rect 바깥 2px 지점을 읽는다.
//1px 이 아니라 2px 인 이유: 변에 따라 rect 경계 픽셀에 테두리 AA 가 걸쳐 있는 경우가 있다.
struct BorderSample
{
	BYTE	obs[3];
	BYTE	bg[3];
};

static void collect_border_samples(const BYTE* px, int w, int h, int ring, int corner_skip,
	const BYTE* frozen, int frozen_w, int frozen_h, int ox, int oy,
	std::vector<BorderSample>& out)
{
	out.clear();

	const int out_dist = 2;

	const int x0 = ring;
	const int x1 = w - 1 - ring;
	const int y0 = ring;
	const int y1 = h - 1 - ring;
	if (x1 - x0 < 2 * corner_skip || y1 - y0 < 2 * corner_skip)
		return;

	auto push = [&](int x, int y, int bx, int by)
	{
		if (bx < 0 || by < 0 || bx >= frozen_w || by >= frozen_h)
			return;

		const BYTE* p = px + (size_t(y) * size_t(w) + size_t(x)) * 4;
		const BYTE* q = frozen + (size_t(by) * size_t(frozen_w) + size_t(bx)) * 4;

		BorderSample s;
		s.obs[0] = p[0];
		s.obs[1] = p[1];
		s.obs[2] = p[2];
		s.bg[0] = q[0];
		s.bg[1] = q[1];
		s.bg[2] = q[2];
		out.push_back(s);
	};

	for (int x = x0 + corner_skip; x <= x1 - corner_skip; ++x)
	{
		push(x, y0, ox + x, oy - out_dist);
		push(x, y1, ox + x, oy + h - 1 + out_dist);
	}
	for (int y = y0 + corner_skip; y <= y1 - corner_skip; ++y)
	{
		push(x0, y, ox - out_dist, oy + y);
		push(x1, y, ox + w - 1 + out_dist, oy + y);
	}
}

//20260727 by claude. 관측 = C*a + 배경*(1-a) 를 배경에 대한 1차식으로 보면 기울기 = (1-a),
//절편 = C*a 이므로 채널별 최소제곱으로 둘 다 얻는다.
//
//반환하는 값은 순수 테두리 색 C 가 아니라 "흰 배경 위에 놓였을 때의 색" = C*a + 255*(1-a) 다.
//DWM 테두리는 반투명이라 흰 배경에서는 연하게 보이는데(실측: 탐색기 검정 위 47, 흰색 위 177),
//캡처는 대개 흰 문서에 붙이므로 그 모습이 화면에서 보던 인상과 가장 가깝다.
//절편 + 255*기울기 로 바로 나오므로 (1-기울기) 로 나누지 않아 오차 증폭도 없다.
//
//배경이 균일해도 대개 성립한다 — DWM 그림자가 변마다 다르게 깔려(실측: 흰 배경에서 상 239 /
//좌우 219 / 하 201) 네 변이 서로 다른 실효 배경을 갖기 때문이다.
//
//예외는 검은 배경이다. 그림자가 검정 위에 깔려도 여전히 검정이라 배경 변화가 0 이고 기울기를
//구할 수 없다. 이때는 실측한 DWM 표준 투명도를 상수로 가정한다(1-a = 0.6; 실측 0.63 / 0.53).
//대상 창이 불투명 테두리를 쓴다면 이 가정이 틀려 실제보다 밝게 나오지만, 배경이 균일한 상황에서는
//관측값만으로 반투명/불투명을 구분할 방법이 없다.
static bool solve_border_color(const std::vector<BorderSample>& v, BYTE out_bgr[3])
{
	if (v.size() < 32)
		return false;

	const double n = double(v.size());

	for (int ch = 0; ch < 3; ++ch)
	{
		double sx = 0.0;
		double sy = 0.0;
		for (const BorderSample& s : v)
		{
			sx += double(s.bg[ch]);
			sy += double(s.obs[ch]);
		}
		const double mx = sx / n;
		const double my = sy / n;

		double sxx = 0.0;
		double sxy = 0.0;
		for (const BorderSample& s : v)
		{
			const double dx = double(s.bg[ch]) - mx;
			sxx += dx * dx;
			sxy += dx * (double(s.obs[ch]) - my);
		}

		//배경 표준편차 4 미만이면 기울기가 잡음에 휘둘린다 → 실측 상수로 대체.
		const double default_slope = 0.6;
		double slope = default_slope;
		if (sxx / n >= 16.0)
		{
			slope = sxy / sxx;
			if (slope < 0.0)
				slope = 0.0;
			if (slope > 1.0)
				slope = 1.0;
		}

		double c = (my - slope * mx) + 255.0 * slope;
		if (c < 0.0)
			c = 0.0;
		if (c > 255.0)
			c = 255.0;

		out_bgr[ch] = BYTE(c + 0.5);
	}
	return true;
}

//20260727 by claude. 링의 채널별 중앙값. 평균이 아니라 중앙값인 이유는 창이 어두운 물체나
//밝은 창 위에 일부만 걸쳐 있을 때 그 구간이 평균을 끌고 가기 때문.
static void ring_median_color(const std::vector<DWORD>& v, BYTE out_bgr[3])
{
	std::vector<BYTE> ch(v.size());
	for (int c = 0; c < 3; ++c)
	{
		const int shift = c * 8;
		for (size_t i = 0; i < v.size(); ++i)
			ch[i] = BYTE((v[i] >> shift) & 0xFF);

		const size_t mid = ch.size() / 2;
		std::nth_element(ch.begin(), ch.begin() + mid, ch.end());
		out_bgr[c] = ch[mid];
	}
}

//20260727 by claude. 화면 BitBlt 로 읽은 창 가장자리에는 DWM 이 반투명으로 그린 테두리가
//"테두리색 * a + 뒤 배경 * (1-a)" 형태로 이미 합성돼 들어온다. 그 링을 실제 테두리 색으로 덮는다.
//
//오염 두께는 모니터 배율과 대상 앱의 DPI 정책에 따라 달라지므로(실측: 100%=1px, 175%=2px)
//상수로 두지 않고 픽셀에서 판정한다. 판정 기준 = 링 안의 "서로 다른 색 비율" —
//실측상 오염 링 25~92%, 창 콘텐츠 링 0.5~6%.
//
//칠할 색은 배경을 소거한 뒤 흰 배경 기준으로 다시 합성한 값이다. 배경이 어떤 색이었든 같은 창이면
//같은 색이 나오므로 캡처마다 테두리 색이 달라 보이는 문제가 사라진다.
//추정에 실패하면(배경 정보 없음 등) 링 중앙값으로 폴백한다 — 배경에 따라 변하지만 노이즈는 없다.
//
//두께는 호출자가 모니터 DPI 로 계산해 넘긴다. 링의 색 산포로 판정하던 방식은 폐기했다 —
//배경이 단색이면 오염된 링도 균일해서 "오염 아님" 으로 잘못 판정한다(실측: 단색 배경 캡처에서
//보정이 통째로 건너뛰어졌다). DWM 테두리는 논리 1px 이므로 배율에 비례한다(100%=1, 175%=2).
static void repair_dwm_border(BYTE* px, int w, int h, int corner_radius, int thickness,
	const BYTE* frozen, int frozen_w, int frozen_h, int ox, int oy)
{
	const int max_rings = 4;
	const int corner_skip = corner_radius + 4;

	if (thickness < 1)
		thickness = 1;
	if (thickness > max_rings)
		thickness = max_rings;

	std::vector<DWORD> ring;
	BYTE ring_bgr[max_rings][3] = {};

	//가장 안쪽 오염 링에서 색을 구한다. 바깥 링일수록 배경 비중이 커서 추정이 불안정하다.
	//성공하면 모든 링을 그 한 색으로 통일한다 — 링마다 다른 색을 쓰면 배경 농담이 그대로 남는다.
	BYTE solved_bgr[3] = {};
	std::vector<BorderSample> samples;
	bool solved = false;
	if (frozen)
	{
		for (int k = thickness - 1; k >= 0 && !solved; --k)
		{
			collect_border_samples(px, w, h, k, corner_skip, frozen, frozen_w, frozen_h, ox, oy, samples);
			solved = solve_border_color(samples, solved_bgr);
		}
	}

	for (int k = 0; k < thickness; ++k)
	{
		if (solved)
		{
			ring_bgr[k][0] = solved_bgr[0];
			ring_bgr[k][1] = solved_bgr[1];
			ring_bgr[k][2] = solved_bgr[2];
			continue;
		}

		collect_ring_colors(px, w, h, k, corner_skip, ring);
		if (ring.empty())
			return;

		ring_median_color(ring, ring_bgr[k]);
	}

	//20260727 by claude. 흰 배경 기준으로 합성한 값이 눈에는 살짝 밝아 보여 32 만큼 낮춘다.
	const int darken = 32;
	for (int k = 0; k < thickness; ++k)
	{
		for (int c = 0; c < 3; ++c)
		{
			const int v = int(ring_bgr[k][c]) - darken;
			ring_bgr[k][c] = BYTE(v < 0 ? 0 : v);
		}
	}

	auto put = [&](int x, int y, int k)
	{
		BYTE* p = px + (size_t(y) * size_t(w) + size_t(x)) * 4;
		p[0] = ring_bgr[k][0];
		p[1] = ring_bgr[k][1];
		p[2] = ring_bgr[k][2];
		p[3] = 0xFF;
	};

	//직선 구간. 코너는 사각 링으로 덮으면 호 안쪽에 배경 섞인 AA 픽셀이 남으므로 여기서 제외한다.
	for (int k = 0; k < thickness; ++k)
	{
		for (int x = corner_radius; x <= w - 1 - corner_radius; ++x)
		{
			put(x, k, k);
			put(x, h - 1 - k, k);
		}
		for (int y = corner_radius; y <= h - 1 - corner_radius; ++y)
		{
			put(k, y, k);
			put(w - 1 - k, y, k);
		}
	}

	if (corner_radius <= 0)
		return;

	//코너는 호를 따라 칠한다. 중심에서의 거리로 링 번호를 정하므로 직선 구간과 두께가 이어진다.
	//호 바깥(k < 0) 은 손대지 않는다 — 뒤이어 apply_rounded_corner_alpha 가 알파 0 으로 잘라낸다.
	//
	//테두리 안쪽으로는 DWM 이 창 내용을 호 모양으로 잘라내며 만든 AA 띠가 이어진다. 그 픽셀들도
	//배경과 섞여 있어(실측: 검은 배경에서 139/175/212, 흰 배경에서는 창 색과 비슷해 안 보임)
	//같은 반경 방향의 안쪽 픽셀 색으로 메운다. 직선 변에는 이 띠가 없다.
	const int aa_band = 2 * thickness + 1;
	const double r_out = double(corner_radius);
	const double r_in = r_out - double(thickness);

	//20260828 by claude. 반경이 작으면(ROUNDSMALL=4, thickness=1 → r_in 3, aa_band 3) 이 값이 음수가 되어
	//아래 루프가 코너 픽셀을 전부 건너뛰었다. 직선 구간 루프도 코너를 제외하므로 코너만 원본 오염 픽셀
	//(뒤 배경이 섞인 DWM 반투명 테두리) 이 그대로 남아, 변의 테두리와 색이 어긋나 잘린 것처럼 보였다.
	//샘플 반경은 "창 내용이 온전한 가장 바깥 지점" 이면 되므로 1px 아래로 내려갈 이유가 없다.
	double sample_dist = r_in - double(aa_band) - 0.5;
	if (sample_dist < 1.0)
		sample_dist = 1.0;

	auto paint_corner = [&](int x0, int y0, double cx, double cy)
	{
		for (int y = y0; y < y0 + corner_radius; ++y)
		{
			for (int x = x0; x < x0 + corner_radius; ++x)
			{
				const double dx = double(x) + 0.5 - cx;
				const double dy = double(y) + 0.5 - cy;
				const double dist = sqrt(dx * dx + dy * dy);

				//호 바깥은 손대지 않는다. 알파 마스킹이 뒤이어 잘라낸다.
				if (dist > r_out + 0.5)
					continue;
				//AA 띠보다 안쪽은 온전한 창 내용이다.
				if (dist < sample_dist)
					continue;

				const int sx = int(cx + dx / dist * sample_dist);
				const int sy = int(cy + dy / dist * sample_dist);
				if (sx < 0 || sy < 0 || sx >= w || sy >= h)
					continue;

				//안쪽 경계에서 창 내용 ↔ 테두리를 1px 폭으로 선형 혼합해 계단을 없앤다.
				//바깥 경계의 부드러움은 apply_rounded_corner_alpha 의 커버리지 알파가 담당한다.
				double f = dist - r_in + 0.5;
				if (f < 0.0)
					f = 0.0;
				if (f > 1.0)
					f = 1.0;

				const BYTE* s = px + (size_t(sy) * size_t(w) + size_t(sx)) * 4;
				BYTE* d = px + (size_t(y) * size_t(w) + size_t(x)) * 4;
				for (int c = 0; c < 3; ++c)
					d[c] = BYTE(double(s[c]) * (1.0 - f) + double(ring_bgr[0][c]) * f + 0.5);
				d[3] = 0xFF;
			}
		}
	};
	paint_corner(0,                0,                double(corner_radius),     double(corner_radius));
	paint_corner(w - corner_radius, 0,               double(w - corner_radius), double(corner_radius));
	paint_corner(0,                h - corner_radius, double(corner_radius),    double(h - corner_radius));
	paint_corner(w - corner_radius, h - corner_radius, double(w - corner_radius), double(h - corner_radius));
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
	HDC hdc_mem	= ::CreateCompatibleDC(hdc_screen);

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize	= sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth	= w;
	bmi.bmiHeader.biHeight	= -h;	//top-down
	bmi.bmiHeader.biPlanes	= 1;
	bmi.bmiHeader.biBitCount	= 32;
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
	CRect rc_virtual	= dlg.get_virtual_screen_rect();

	//창 캡처 = 프리즈 가상 데스크톱 DIB 에서 hwnd 의 가시 rect 영역만 crop.
	//(rc_highlight 는 SCCaptureOverlayDlg 가 DwmGetWindowAttribute(EXTENDED_FRAME_BOUNDS) 로 잡은 가시 rect.)
	//
	//이전엔 PrintWindow(PW_RENDERFULLCONTENT) 를 우선 사용했으나, 자체 DC 로 그리는
	//커스텀 child 컨트롤이나 WM_PRINTCLIENT 에 응답하지 않는 컴포넌트가 캡처 결과에서
	//통째로 누락되는 알려진 이슈가 있어 frozen DIB crop 으로 일원화. 영역 캡처와 동일 방식.
	const int w_clip = rc_highlight.Width();
	const int h_clip = rc_highlight.Height();
	HBITMAP hbmp_for_clip = NULL;

	if (w_clip > 0 && h_clip > 0)
	{
		HBITMAP hbmp_src = dlg.get_frozen_hbitmap();
		if (hbmp_src)
		{
			HDC hdc_screen = ::GetDC(NULL);
			HDC hdc_src	= ::CreateCompatibleDC(hdc_screen);
			HDC hdc_dst	= ::CreateCompatibleDC(hdc_screen);

			BITMAPINFO bmi = {};
			bmi.bmiHeader.biSize	= sizeof(bmi.bmiHeader);
			bmi.bmiHeader.biWidth	= w_clip;
			bmi.bmiHeader.biHeight	= -h_clip;
			bmi.bmiHeader.biPlanes	= 1;
			bmi.bmiHeader.biBitCount	= 32;
			bmi.bmiHeader.biCompression = BI_RGB;

			void* dst_bits = nullptr;
			hbmp_for_clip = ::CreateDIBSection(hdc_dst, &bmi, DIB_RGB_COLORS, &dst_bits, NULL, 0);
			HGDIOBJ old_src = ::SelectObject(hdc_src, hbmp_src);
			HGDIOBJ old_dst = ::SelectObject(hdc_dst, hbmp_for_clip);

			//프리즈 DIB 좌표 = virtual screen 기준 0,0. screen rect 를 좌상단 기준 좌표로 변환.
			::BitBlt(hdc_dst, 0, 0, w_clip, h_clip,
				hdc_src, rc_highlight.left - rc_virtual.left,
				         rc_highlight.top	- rc_virtual.top,
				SRCCOPY);

			//BI_RGB 32bpp BitBlt 결과의 alpha 채널은 0. 일부 paste target 에서 투명하게 보임 → 0xFF 채움.
			if (dst_bits)
			{
				BYTE* p = static_cast<BYTE*>(dst_bits);
				const int total = w_clip * h_clip;
				for (int i = 0; i < total; ++i)
					p[i * 4 + 3] = 0xFF;
			}

			::SelectObject(hdc_src, old_src);
			::SelectObject(hdc_dst, old_dst);
			::DeleteDC(hdc_src);
			::DeleteDC(hdc_dst);
			::ReleaseDC(NULL, hdc_screen);
		}
	}

	if (hbmp_for_clip)
	{
		DIBSECTION ds = {};
		if (::GetObject(hbmp_for_clip, sizeof(ds), &ds) == sizeof(ds) && ds.dsBm.bmBits)
		{
			BYTE* bits = static_cast<BYTE*>(ds.dsBm.bmBits);
			const int radius = probe_window_corner_radius(hwnd);

			//20260727 by claude. DWM 이 프레임을 그리는 창에만 테두리 보정을 건다. 자체 그린
			//borderless popup 은 최외곽 링이 창 자신의 그림이라 덮으면 실제 콘텐츠가 손상된다.
			const LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
			if (radius > 0 || (style & (WS_CAPTION | WS_THICKFRAME)))
			{
				//순수 테두리 색을 구하려면 창 바깥 배경이 필요하다. 프리즈 DIB 는 가상 데스크톱
				//전체라 창 바깥이 그대로 들어 있다.
				DIBSECTION ds_frozen = {};
				const BYTE* frozen = NULL;
				if (::GetObject(dlg.get_frozen_hbitmap(), sizeof(ds_frozen), &ds_frozen) == sizeof(ds_frozen))
					frozen = static_cast<const BYTE*>(ds_frozen.dsBm.bmBits);

				//DWM 테두리는 논리 1px — 물리 픽셀 두께는 모니터 배율에 비례 (100%=1, 175%=2).
				const int thickness = int((monitor_dpi_for_window(hwnd) + 48) / 96);

				repair_dwm_border(bits, w_clip, h_clip, radius, thickness,
					frozen, rc_virtual.Width(), rc_virtual.Height(),
					rc_highlight.left - rc_virtual.left,
					rc_highlight.top  - rc_virtual.top);
			}

			//Win11 라운드 코너 마스킹: 4 모서리 호 바깥 alpha=0. PNG 저장/D2D 노트 표시 시 투명.
			if (radius > 0)
				apply_rounded_corner_alpha(bits, w_clip, h_clip, radius);
		}
	}

	if (hbmp_for_clip && w_clip > 0 && h_clip > 0)
	{
		DIBSECTION ds = {};
		if (::GetObject(hbmp_for_clip, sizeof(ds), &ds) == sizeof(ds) && ds.dsBm.bmBits)
		{
			POINT pos = { rc_highlight.left, rc_highlight.top };
			send_image_to_clipboard_and_note(
				static_cast<const BYTE*>(ds.dsBm.bmBits),
				w_clip, h_clip, pos);
		}
	}

	if (hbmp_for_clip)
		::DeleteObject(hbmp_for_clip);
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

	CRect rc_sel	= dlg.get_picked_rect_screen();
	CRect rc_virtual = dlg.get_virtual_screen_rect();
	HBITMAP hbmp_src = dlg.get_frozen_hbitmap();

	const int dst_w = rc_sel.Width();
	const int dst_h = rc_sel.Height();
	if (!hbmp_src || dst_w <= 0 || dst_h <= 0)
		return;

	//1) 프리즈 DIB 의 sub-region 을 새 32bpp top-down DIB section 으로 BitBlt.
	HDC hdc_screen = ::GetDC(NULL);
	HDC hdc_src	= ::CreateCompatibleDC(hdc_screen);
	HDC hdc_dst	= ::CreateCompatibleDC(hdc_screen);

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize	= sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth	= dst_w;
	bmi.bmiHeader.biHeight	= -dst_h;	//top-down
	bmi.bmiHeader.biPlanes	= 1;
	bmi.bmiHeader.biBitCount	= 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* dst_bits = nullptr;
	HBITMAP hbmp_dst = ::CreateDIBSection(hdc_dst, &bmi, DIB_RGB_COLORS, &dst_bits, NULL, 0);
	HGDIOBJ old_src = ::SelectObject(hdc_src, hbmp_src);
	HGDIOBJ old_dst = ::SelectObject(hdc_dst, hbmp_dst);

	::BitBlt(hdc_dst, 0, 0, dst_w, dst_h,
		hdc_src,
		rc_sel.left - rc_virtual.left,
		rc_sel.top	- rc_virtual.top,
		SRCCOPY);

	::SelectObject(hdc_src, old_src);
	::SelectObject(hdc_dst, old_dst);
	::DeleteDC(hdc_src);
	::DeleteDC(hdc_dst);
	::ReleaseDC(NULL, hdc_screen);

	//2) 클립보드 + floating note ? 헬퍼로 일원화.
	if (hbmp_dst && dst_bits)
	{
		POINT pos = { rc_sel.left, rc_sel.top };
		send_image_to_clipboard_and_note(static_cast<const BYTE*>(dst_bits), dst_w, dst_h, pos);
	}

	if (hbmp_dst)
		::DeleteObject(hbmp_dst);
}

void CSCDeskToolsDlg::OnToolCaptureFreehand()
{
	HideFloating hide(this);
	Wait(200);

	CSCFreehandCaptureDlg dlg;
	if (!dlg.create(this))
	{
		AfxMessageBox(_T("자유 영역 캡처 오버레이 생성 실패"));
		return;
	}

	dlg.run_modal_loop(this);

	if (!dlg.is_picked())
		return;

	const std::vector<CPoint>& path_screen = dlg.get_picked_path_screen();
	const CRect rc_bounds  = dlg.get_picked_bounds_screen();
	const CRect rc_virtual = dlg.get_virtual_screen_rect();
	HBITMAP hbmp_src = dlg.get_frozen_hbitmap();

	const int dst_w = rc_bounds.Width();
	const int dst_h = rc_bounds.Height();
	if (!hbmp_src || dst_w <= 0 || dst_h <= 0 || path_screen.size() < 3)
		return;

	//1) 프리즈 DIB 의 bounding box 영역을 새 32bpp top-down DIB section 으로 BitBlt.
	HDC hdc_screen = ::GetDC(NULL);
	HDC hdc_src	= ::CreateCompatibleDC(hdc_screen);
	HDC hdc_dst	= ::CreateCompatibleDC(hdc_screen);

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize	= sizeof(bmi.bmiHeader);
	bmi.bmiHeader.biWidth	= dst_w;
	bmi.bmiHeader.biHeight	= -dst_h;
	bmi.bmiHeader.biPlanes	= 1;
	bmi.bmiHeader.biBitCount	= 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* dst_bits = nullptr;
	HBITMAP hbmp_dst = ::CreateDIBSection(hdc_dst, &bmi, DIB_RGB_COLORS, &dst_bits, NULL, 0);
	HGDIOBJ old_src = ::SelectObject(hdc_src, hbmp_src);
	HGDIOBJ old_dst = ::SelectObject(hdc_dst, hbmp_dst);

	::BitBlt(hdc_dst, 0, 0, dst_w, dst_h,
		hdc_src,
		rc_bounds.left - rc_virtual.left,
		rc_bounds.top  - rc_virtual.top,
		SRCCOPY);

	::SelectObject(hdc_src, old_src);
	::SelectObject(hdc_dst, old_dst);
	::DeleteDC(hdc_src);
	::DeleteDC(hdc_dst);
	::ReleaseDC(NULL, hdc_screen);

	//2) 폴리곤 영역 외 픽셀 alpha=0. CreatePolygonRgn + PtInRegion 으로 픽셀 단위 판정.
	if (hbmp_dst && dst_bits)
	{
		std::vector<POINT> pts_local;
		pts_local.reserve(path_screen.size());
		for (const CPoint& p : path_screen)
		{
			POINT lp = { p.x - rc_bounds.left, p.y - rc_bounds.top };
			pts_local.push_back(lp);
		}
		HRGN hrgn = ::CreatePolygonRgn(pts_local.data(), int(pts_local.size()), WINDING);
		if (hrgn)
		{
			BYTE* px = static_cast<BYTE*>(dst_bits);
			for (int y = 0; y < dst_h; ++y)
			{
				BYTE* row = px + size_t(y) * size_t(dst_w) * 4;
				for (int x = 0; x < dst_w; ++x)
				{
					if (::PtInRegion(hrgn, x, y))
					{
						row[x * 4 + 3] = 0xFF;
					}
					else
					{
						row[x * 4 + 0] = 0;
						row[x * 4 + 1] = 0;
						row[x * 4 + 2] = 0;
						row[x * 4 + 3] = 0;
					}
				}
			}
			::DeleteObject(hrgn);
		}

		POINT pos = { rc_bounds.left, rc_bounds.top };
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
	//테스트용으로 매번 캡처할 필요 없게 한다 ? 다른 앱에서 Ctrl+C 한 이미지를 붙여넣기.
	if (!::OpenClipboard(m_hWnd))
	{
		AfxMessageBox(_T("클립보드 열기 실패"));
		return;
	}

	bool ok = false;
	int width = 0;
	int height = 0;
	std::vector<BYTE> bgra;	//top-down 32bpp BGRA

	//1) PNG 우선 — alpha 보존 (freehand / 라운드 코너 캡처 등 투명 영역 살림).
	const UINT cf_png = ::RegisterClipboardFormat(_T("PNG"));
	if (cf_png && ::IsClipboardFormatAvailable(cf_png))
	{
		HANDLE hp = ::GetClipboardData(cf_png);
		if (hp && decode_png_hglobal_to_bgra(static_cast<HGLOBAL>(hp), bgra, width, height))
			ok = true;
	}

	HANDLE h_data = ok ? NULL : ::GetClipboardData(CF_DIB);
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
