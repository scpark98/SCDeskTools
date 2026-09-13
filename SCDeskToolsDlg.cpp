
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
#include "Common/win_compat/dpi.h"
#include "Common/ocr/SCWinOcr.h"
#include "Common/log/SCLog/SCLog.h"
#include "Common/SCGdiplusBitmap.h"
#include "Common/colors.h"
#include <map>
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

//본 파일 하단 정의를 앞쪽 사용처(레이아웃)에서 참조 가능하게 forward declaration.
static UINT monitor_dpi_for_window(HWND hwnd);

//20260912 by claude. 토스트 패널 기본색. OCR 결과처럼 원본 배경을 가져오는 경우가 아니면 이 색을 쓴다.
static const Gdiplus::Color toast_back_default = Gdiplus::Color(232, 24, 24, 24);

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
		{ ID_TOOL_OCR_CLIPBOARD,		_T("클립보드 이미지 텍스트 인식"),	_T("클립보드 이미지 텍스트 인식"),	cat_capture },
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
		{ 10,	ID_TOOL_OCR_CLIPBOARD,		MOD_ALT | MOD_SHIFT | MOD_NOREPEAT,	'T',	_T("클립보드 이미지 텍스트 인식 (Alt+Shift+T)") },
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
	ON_COMMAND(ID_TOOL_OCR_CLIPBOARD, &CSCDeskToolsDlg::OnToolOcrClipboard)
	ON_COMMAND(ID_TOOL_PROTRACTOR, &CSCDeskToolsDlg::OnToolProtractor)
	ON_COMMAND(ID_TOOL_RULER, &CSCDeskToolsDlg::OnToolRuler)
	ON_COMMAND(ID_APP_SHOW_HIDE, &CSCDeskToolsDlg::OnAppShowHide)
	ON_COMMAND(ID_HELP_ABOUT, &CSCDeskToolsDlg::OnAppAbout)
	ON_COMMAND(ID_SC_EXIT, &CSCDeskToolsDlg::OnAppExit)
	ON_BN_CLICKED(IDOK, &CSCDeskToolsDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CSCDeskToolsDlg::OnBnClickedCancel)
	ON_BN_CLICKED(id_check_clipboard_only, &CSCDeskToolsDlg::OnBnClickedClipboardOnly)
	ON_WM_WINDOWPOSCHANGED()
	ON_MESSAGE(WM_DPICHANGED, &CSCDeskToolsDlg::on_dpi_changed)
	ON_MESSAGE(Message_ApplyDpiLayout, &CSCDeskToolsDlg::on_message_ApplyDpiLayout)
	ON_WM_ENTERSIZEMOVE()
	ON_WM_EXITSIZEMOVE()
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

	//20260904 by claude. 창 크기 / 폰트 / 자식 배치를 한 함수로 모은다. 시작할 때와 모니터를 옮겼을 때가
	//같은 코드를 타야 두 경로의 결과가 어긋나지 않는다 (아래 apply_dpi_layout 주석 참조).
	m_dpi = monitor_dpi_for_window(m_hWnd);
	apply_dpi_layout(NULL);

	m_sys_tray.SetParent(m_hWnd);
	HICON hIconTray = ::AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_sys_tray.CreateIcon(hIconTray, 1, _T("SCDeskTools"));
	m_sys_tray.ShowIcon(1);

	//modeless 컬러 피커. 처음에는 보이지 않게 두고, 메뉴 클릭 시 ShowWindow 로 토글한다.
	m_color_picker.create(this, _T("Color Picker"), false);

	//20260912 by claude. floating 메시지. 내용이 임의 길이의 본문(OCR 결과)일 수 있어
	//어두운 라운드 패널 + 흰 글자로 둔다 — 어떤 바탕화면 위에 떠도 대비가 유지된다.
	//글자 외곽선 대신 패널로 대비를 만드는 쪽이 본문 가독성에는 유리하다.
	//크기(폰트·라운드·여백)는 띄울 모니터의 DPI 를 따라야 하므로 show_message 에서 매번 정한다.
	m_message.set_text(this, _T(""), 20, Gdiplus::FontStyleRegular, 0.0f, 0.0f, _T("맑은 고딕"),
		Gdiplus::Color::White, Gdiplus::Color::Transparent, Gdiplus::Color::Transparent,
		toast_back_default);
	m_message.set_text_align(DT_LEFT);
	m_message.use_control(false);

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

int CSCDeskToolsDlg::scaled(int px_at_96dpi) const
{
	return ::MulDiv(px_at_96dpi, m_dpi, 96);
}

//20260904 by claude. 시작 시점의 다이얼로그 폰트와 그때의 DPI 를 기준으로, 지금 DPI 비율만큼
//키우거나 줄인 폰트를 만든다. GetFont() 를 그대로 쓰지 않는 이유는 apply_dpi_layout 주석 참조.
void CSCDeskToolsDlg::rebuild_ui_font()
{
	if (m_lf_base.lfHeight == 0)
	{
		CFont* font = GetFont();
		if (!font || font->GetLogFont(&m_lf_base) == 0)
			return;
		m_dpi_base = m_dpi;
	}

	LOGFONT lf = m_lf_base;
	lf.lfHeight = ::MulDiv(m_lf_base.lfHeight, m_dpi, m_dpi_base);
	lf.lfWidth = 0;

	m_font_ui.DeleteObject();
	m_font_ui.CreateFontIndirect(&lf);
}

//20260904 by claude. 창 크기 · 폰트 · 자식 배치를 m_dpi 하나로부터 전부 다시 만든다.
//시작할 때(OnInitDialog)와 모니터를 옮겼을 때(WM_DPICHANGED)가 반드시 이 함수 하나를 타야 한다.
//
//[여기서 겪은 것 — 두 번 다 두 값이 서로 다른 DPI 에서 나와 어긋난 것이었다]
// 1) 배치만 scaled() 로 고치고 폰트를 OS 에 맡겼더니, 100% 모니터에서 버튼만 작아지고 글자는 175% 로
//    남았다. Per-Monitor V2 의 다이얼로그 자동 폰트 스케일링은 WM_DPICHANGED 의 *기본 처리* 안에서
//    일어나는데, ON_MESSAGE 로 가로채 0 을 돌려주면 그게 돌지 않는다. → 폰트도 직접 만든다.
// 2) 창 크기를 OS 권장 rect(이전 창 rect × 배율비) 로 잡았더니 우리 레이아웃 규칙(클라이언트 400x288)과
//    달라 클라이언트가 짧아지고 마지막 행이 잘렸다. → 권장 rect 는 *위치* 로만 쓰고 크기는 계산한다.
//
//pos_hint: WM_DPICHANGED 가 준 권장 rect. NULL 이면 위치를 바꾸지 않는다.
void CSCDeskToolsDlg::apply_dpi_layout(const RECT* pos_hint)
{
	rebuild_ui_font();

	//높이 = top_margin(14) + 4행 * btn_h(50) + 3 gap * 10 + checkbox_gap(10) + checkbox_h(20) + bottom_margin(14) = 288.
	//.rc 의 디폴트(480x325) 보다 작게 — 8 즐겨찾기 + 종료 버튼 + 체크박스가 깔끔히 들어가는 컴팩트 크기.
	CRect target(0, 0, scaled(400), scaled(288));
	win_compat::dpi::adjust_window_rect(&target, GetStyle(), FALSE, GetExStyle(), m_dpi);

	UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
	int x = 0;
	int y = 0;

	if (pos_hint)
	{
		x = pos_hint->left;
		y = pos_hint->top;
	}
	else
	{
		flags |= SWP_NOMOVE;
	}

	SetWindowPos(NULL, x, y, target.Width(), target.Height(), flags);

	//자식들은 Create 시점의 rect 로 고정돼 있어 DestroyWindow 후 다시 만드는 편이 확실하다.
	//build_* 가 GetClientRect 로 배치하므로 위 SetWindowPos 뒤에 와야 한다.
	for (auto& btn : m_buttons_favorite)
	{
		if (btn && btn->GetSafeHwnd())
			btn->DestroyWindow();
	}
	m_buttons_favorite.clear();

	if (m_button_exit.GetSafeHwnd())
		m_button_exit.DestroyWindow();
	if (m_check_clipboard_only.GetSafeHwnd())
		m_check_clipboard_only.DestroyWindow();

	build_buttons();
	build_exit_button();
	build_clipboard_only_checkbox();

	Invalidate();
}

LRESULT CSCDeskToolsDlg::on_dpi_changed(WPARAM wParam, LPARAM lParam)
{
	//새 DPI 는 wParam 이 알려준다. 이 시점에 창은 아직 옛 모니터에 걸쳐 있어
	//MonitorFromWindow 로 물으면 옛 값이 나올 수 있다.
	m_dpi = LOWORD(wParam);
	if (m_dpi == 0)
		m_dpi = 96;

	//OS 권장 rect 를 먼저 적용한다 — 이걸 하지 않으면 창이 커서를 따라오지 않는다.
	//크기는 여기서 잡아봐야 소용없다 (Message_ApplyDpiLayout 주석 참조). 위치만 의미가 있다.
	const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
	if (suggested)
	{
		SetWindowPos(NULL,
			suggested->left, suggested->top,
			suggested->right - suggested->left,
			suggested->bottom - suggested->top,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}

	//실제 레이아웃은 이동이 완전히 끝난 뒤에. 여기서 바로 하면 진행 중인 이동 동작이
	//변경 전 크기를 도로 덮어써서 클라이언트가 옛 배율 크기로 남는다 (측정으로 확인).
	m_dpi_layout_pending = true;
	PostMessage(Message_ApplyDpiLayout);
	return 0;
}

LRESULT CSCDeskToolsDlg::on_message_ApplyDpiLayout(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	//마우스 드래그 중이면 이 메시지도 modal move loop 안에서 처리된다 — 여기서 크기를 잡아봐야
	//루프가 끝나면서 다시 덮어쓴다. 그 경우는 OnExitSizeMove 가 맡는다.
	if (m_in_size_move)
		return 0;

	m_dpi_layout_pending = false;
	apply_dpi_layout(NULL);	//위치는 그대로 두고 크기 / 폰트 / 자식 배치만 다시 잡는다.
	return 0;
}

void CSCDeskToolsDlg::OnEnterSizeMove()
{
	m_in_size_move = true;
	CDialogEx::OnEnterSizeMove();
}

void CSCDeskToolsDlg::OnExitSizeMove()
{
	m_in_size_move = false;
	CDialogEx::OnExitSizeMove();

	//드래그로 다른 배율 모니터에 넘어간 경우. 루프가 끝난 지금이 크기를 잡을 수 있는 첫 시점이다.
	if (m_dpi_layout_pending)
	{
		m_dpi_layout_pending = false;
		apply_dpi_layout(NULL);
	}
}

void CSCDeskToolsDlg::build_buttons()
{
	//m_favorites 의 ID 들을 순서대로 텍스트 버튼으로 배치. ID 는 ON_COMMAND 가 자동 처리.
	//추후 아이콘 포함 버튼 (CSCButton 등) 으로 교체 가능.
	m_buttons_favorite.clear();

	CRect rc_client;
	GetClientRect(rc_client);

	const int margin = scaled(14);
	const int gap_x	= scaled(10);
	const int gap_y	= scaled(10);
	const int cols	= 2;
	const int btn_w	= (rc_client.Width() - margin * 2 - gap_x * (cols - 1)) / cols;
	//두 줄 캡션 (기능명 / 글로벌 단축키) 수용 위해 한 줄짜리 36 → 50.
	const int btn_h	= scaled(50);

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
		btn->SetFont(&m_font_ui);
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

	const int margin = scaled(14);
	const int btn_w	= scaled(80);
	const int btn_h	= scaled(32);

	CRect rc_btn(
		rc_client.right	- margin - btn_w,
		rc_client.bottom - margin - btn_h,
		rc_client.right	- margin,
		rc_client.bottom - margin);

	m_button_exit.Create(_T("종료"),
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
		rc_btn, this, ID_SC_EXIT);
	m_button_exit.SetFont(&m_font_ui);
}

void CSCDeskToolsDlg::build_clipboard_only_checkbox()
{
	CRect rc_client;
	GetClientRect(rc_client);

	const int margin = scaled(14);
	const int check_h = scaled(20);

	CRect rc_check(
		rc_client.left	+ margin,
		rc_client.bottom - margin - check_h,
		rc_client.right	- margin,
		rc_client.bottom - margin);

	m_check_clipboard_only.Create(_T("캡처 시 클립보드로만 저장(플로팅 창으로 띠우지 않음)"),
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
		rc_check, this, id_check_clipboard_only);
	m_check_clipboard_only.SetFont(&m_font_ui);
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
			if ((t.id == ID_TOOL_PASTE_CLIPBOARD || t.id == ID_TOOL_OCR_CLIPBOARD) && !clipboard_has_image())
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
static void ring_median_color(const std::vector<DWORD>& v, BYTE out_bgr[3]);

//20260912 by claude. OCR 단어 하나의 겉모습. 엔진은 글자 색도 크기도 알려주지 않는다 —
//단어 상자 안의 원본 픽셀을 되짚어 직접 뽑는 수밖에 없다.
//20260912 by claude. Windows OCR 은 글자 크기에 매우 민감하다. 어느 배율이 맞는지는 계산으로 알 수 없어
//(엔진이 못 찾은 글자는 어떤 통계에도 안 잡힌다) 여러 배율로 실제로 넣어 보고 결과로 고른다.
//20260913 by claude. 덤프 16장 정답셋으로 후보 집합을 실측해 정했다 — 6개면 9개와 거의 같고(0.926 vs 0.929)
//후보 개수가 곧 인식 횟수다. 간격을 더 벌리지 말 것: 결과가 배율에 단조롭지 않아 사이를 지나치면 놓친다.
static const double ocr_scale_candidates[] = { 1.0, 1.2, 1.4, 1.6, 2.0, 2.5 };

//20260913 by claude. 확대 전 원본에 한 번 거는 언샤프. 실측(정답셋 16장, 합의 선택 기준):
//  전처리 없음 0.899 / 회색조 0.904 / 자동대비 0.905 / 선명화 0.927
//강도를 더 올리면 오히려 나빠진다 (percent 200 에서 0.902). 이 값을 함부로 키우지 말 것.
static const int ocr_unsharp_percent = 160;
static const int ocr_unsharp_threshold = 2;

//OcrEngine::MaxImageDimension. 이 머신에서 10000 으로 확인했다. 확대가 이 한도를 넘으면 안 된다.
static const int ocr_max_image_dimension = 10000;

//확대는 면적에 비례해 메모리와 시간을 먹는다. 4K 전체 캡처를 x2.5 하면 200MB 가 넘는다.
static const double ocr_max_pixels = 40.0 * 1000.0 * 1000.0;

struct ocr_word_style
{
	Gdiplus::Color	cr_text = Gdiplus::Color::Black;
	Gdiplus::Color	cr_back = Gdiplus::Color::White;
	float			pt = 10.0f;
};

//20260912 by claude. 잉크로 칠 픽셀의 최소 밝기 차. 배경과 이만큼 떨어지면 글자로 본다.
static const int ocr_ink_luminance_min = 48;

//20260912 by claude. 색이 "갈렸다" 고 볼 채널합 거리. 실측(빙수가 / 빚까지 자막)에서 같은 색 안의
//열 간 편차는 41 이하, 다른 색 사이는 92 이상으로 뚜렷하게 나뉘었다. 그 사이를 잡는다.
static const int ocr_color_split_distance = 64;

//경계의 안티에일리어싱 한두 열에 흔들리지 않도록, 새 색이 이만큼 연속돼야 색이 바뀐 것으로 인정한다.
static const int ocr_color_split_run = 3;

//20260912 by claude. 단어 상자의 배경색. 상자 "바깥" 테두리에서 잡는다.
//처음에는 상자 안의 최빈색을 배경으로 봤는데 그게 틀렸다 — 크고 굵은 글자는 잉크가 상자의 절반을
//넘어서, 검정 글자가 배경으로 흰 여백이 글자로 뒤집혔다 (실측: '150매' 가 글자=#D2CCCA 배경=#221D1B).
//상자 바깥은 잉크 비율과 무관하게 배경이다.
static Gdiplus::Color sample_back_color(const BYTE* bgra, int img_w, int img_h, CRect rc)
{
	CRect rc_ring = rc;
	rc_ring.InflateRect(max(2, rc.Height() / 8), max(2, rc.Height() / 8));
	rc_ring.left    = max(rc_ring.left, 0);
	rc_ring.top     = max(rc_ring.top, 0);
	rc_ring.right   = min(rc_ring.right, img_w);
	rc_ring.bottom  = min(rc_ring.bottom, img_h);

	std::vector<DWORD> ring;
	for (int y = rc_ring.top; y < rc_ring.bottom; ++y)
	{
		for (int x = rc_ring.left; x < rc_ring.right; ++x)
		{
			if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom)
				continue;

			const BYTE* px = bgra + ((size_t)y * img_w + x) * 4;
			ring.push_back((DWORD)px[0] | ((DWORD)px[1] << 8) | ((DWORD)px[2] << 16));
		}
	}

	if (ring.size() < 8)
		return Gdiplus::Color::Black;   //상자가 이미지 가장자리에 붙은 경우 — 호출자가 대비로 보정한다.

	BYTE bgr[3] = { 0, 0, 0 };
	ring_median_color(ring, bgr);
	return Gdiplus::Color(255, bgr[2], bgr[1], bgr[0]);
}

static int color_luminance(Gdiplus::Color cr)
{
	return (cr.GetR() * 299 + cr.GetG() * 587 + cr.GetB() * 114) / 1000;
}

static int color_distance(Gdiplus::Color a, Gdiplus::Color b)
{
	return abs((int)a.GetR() - (int)b.GetR())
		+ abs((int)a.GetG() - (int)b.GetG())
		+ abs((int)a.GetB() - (int)b.GetB());
}

//20260912 by claude. 상자 안에서 배경과 밝기가 가장 먼 색 = 글자색. 안티에일리어싱 때문에 중간 톤이
//잔뜩 끼므로 "최빈" 이 아니라 "최원" 이 실제 잉크다. 점 하나짜리 노이즈를 피하려고 최소 픽셀 수를 둔다.
//대비가 거의 없으면 배경 위에서 읽히는 흑/백으로 물러선다.
static Gdiplus::Color sample_ink_color(const BYTE* bgra, int img_w, int img_h, CRect rc, Gdiplus::Color cr_back)
{
	struct bucket
	{
		int count = 0;
		int b = 0;
		int g = 0;
		int r = 0;
	};
	std::map<DWORD, bucket> hist;

	for (int y = rc.top; y < rc.bottom; ++y)
	{
		for (int x = rc.left; x < rc.right; ++x)
		{
			const BYTE* px = bgra + ((size_t)y * img_w + x) * 4;
			const DWORD key = ((DWORD)(px[2] >> 3) << 10) | ((DWORD)(px[1] >> 3) << 5) | (DWORD)(px[0] >> 3);

			bucket& bk = hist[key];
			bk.count++;
			bk.b += px[0];
			bk.g += px[1];
			bk.r += px[2];
		}
	}
	if (hist.empty())
		return get_readable_text_color(cr_back);

	const int lum_back = color_luminance(cr_back);
	const int min_count = max(4, (rc.Width() * rc.Height()) / 100);

	Gdiplus::Color ink = cr_back;
	int farthest = -1;

	for (const auto& kv : hist)
	{
		if (kv.second.count < min_count)
			continue;

		const Gdiplus::Color cr(255,
			(BYTE)(kv.second.r / kv.second.count),
			(BYTE)(kv.second.g / kv.second.count),
			(BYTE)(kv.second.b / kv.second.count));

		const int distance = abs(color_luminance(cr) - lum_back);
		if (distance > farthest)
		{
			farthest = distance;
			ink = cr;
		}
	}

	if (farthest < 24)
		return get_readable_text_color(cr_back);
	return ink;
}

//20260912 by claude. 단어 상자 높이로 글자 크기를 환산한다.
//[가정] 단어 상자 높이 = 글자 px 의 약 0.92 배. 11~38px 에서 실측했으나 표본이 맑은 고딕 합성 샘플
//한 종류뿐이다. 서체·장평이 다르면 어긋난다 — 틀려도 글자 크기(pt)만 달라지고 인식에는 영향이 없다.
static float word_point_size(int word_height, UINT dpi)
{
	const float em_px = (word_height > 0) ? (word_height / 0.92f) : 16.0f;
	return em_px * 72.0f / (float)(dpi > 0 ? dpi : 96);
}

static ocr_word_style sample_word_style(const BYTE* bgra, int img_w, int img_h,
										CRect rc, UINT dpi, int word_height)
{
	ocr_word_style style;
	style.pt = word_point_size(word_height, dpi);

	rc.left     = max(rc.left, 0);
	rc.top      = max(rc.top, 0);
	rc.right    = min(rc.right, img_w);
	rc.bottom   = min(rc.bottom, img_h);
	if (rc.Width() <= 0 || rc.Height() <= 0)
		return style;

	style.cr_back = sample_back_color(bgra, img_w, img_h, rc);
	style.cr_text = sample_ink_color(bgra, img_w, img_h, rc, style.cr_back);
	return style;
}

//20260912 by claude. 한 단어 안에서 글자색이 바뀌면 그 지점에서 쪼갠다.
//엔진은 단어 단위로만 돌려주므로 "빙수가"(핑크+흰색), "빚까지"(연두+주황) 처럼 색이 섞인 단어가
//통째로 한 색이 되어 버린다 — 그것도 밝은 쪽이 이기므로 핑크가 크림색으로 둔갑했다.
//
//열마다 잉크 색을 재어 색이 유지되는 구간(run)으로 나눈 뒤, 글자를 그 구간에 배분한다.
//[가정] 한 단어 안의 글자 폭은 고르다. 구간 경계가 몇 번째 글자에 해당하는지를 폭 비율로 정하기
//때문이다. 한글은 음절 폭이 고르므로 잘 맞는다 (실측: 빙수가 0.69 vs 2/3, 빚까지 0.31 vs 1/3).
//라틴 문자처럼 폭이 들쭉날쭉하면 경계가 한 글자 어긋날 수 있다. 색이 하나뿐인 단어는 영향 없다.
static void split_word_by_color(const BYTE* bgra, int img_w, int img_h, CRect rc, UINT dpi,
								int word_height, const CString& text,
								std::vector<CString>& out_text, std::vector<ocr_word_style>& out_style)
{
	const ocr_word_style whole = sample_word_style(bgra, img_w, img_h, rc, dpi, word_height);

	rc.left     = max(rc.left, 0);
	rc.top      = max(rc.top, 0);
	rc.right    = min(rc.right, img_w);
	rc.bottom   = min(rc.bottom, img_h);

	const int count = text.GetLength();
	if (count <= 1 || rc.Width() <= 0 || rc.Height() <= 0)
	{
		out_text.push_back(text);
		out_style.push_back(whole);
		return;
	}

	const int lum_back = color_luminance(whole.cr_back);

	//열마다 잉크 픽셀의 평균색. 잉크가 없거나 획이 얇게 스치기만 하는 열은 색이 흐려 기준이 못 된다 —
	//그런 열을 기준으로 삼으면 단색 단어까지 잘게 쪼개진다 (실측: '아이고' 가 3조각).
	const int min_ink = max(2, rc.Height() / 6);

	std::vector<Gdiplus::Color> column(rc.Width(), Gdiplus::Color::Transparent);
	for (int x = rc.left; x < rc.right; ++x)
	{
		int n = 0;
		int sr = 0;
		int sg = 0;
		int sb = 0;

		for (int y = rc.top; y < rc.bottom; ++y)
		{
			const BYTE* px = bgra + ((size_t)y * img_w + x) * 4;
			const Gdiplus::Color cr(255, px[2], px[1], px[0]);
			if (abs(color_luminance(cr) - lum_back) < ocr_ink_luminance_min)
				continue;

			n++;
			sr += px[2];
			sg += px[1];
			sb += px[0];
		}

		if (n >= min_ink)
			column[x - rc.left] = Gdiplus::Color(255, (BYTE)(sr / n), (BYTE)(sg / n), (BYTE)(sb / n));
	}

	//색이 유지되는 구간으로 나눈다. 비교 기준은 그 구간의 "평균" 색이다 — 시작 열 하나를 기준으로
	//삼으면 그 열이 어두운 가장자리일 때 같은 색인데도 갈라진다. 경계의 흔들림은 연속 조건으로 무시한다.
	std::vector<int> run_start;     //구간 시작 (rc.left 기준 offset)
	int acc_r = 0;
	int acc_g = 0;
	int acc_b = 0;
	int acc_n = 0;

	std::vector<int> pending_index;
	std::vector<Gdiplus::Color> pending_color;

	for (int i = 0; i < (int)column.size(); ++i)
	{
		if (column[i].GetA() == 0)
			continue;

		if (run_start.empty())
		{
			run_start.push_back(i);
			acc_r = column[i].GetR();
			acc_g = column[i].GetG();
			acc_b = column[i].GetB();
			acc_n = 1;
			continue;
		}

		const Gdiplus::Color average(255, (BYTE)(acc_r / acc_n), (BYTE)(acc_g / acc_n), (BYTE)(acc_b / acc_n));

		if (color_distance(column[i], average) <= ocr_color_split_distance)
		{
			pending_index.clear();
			pending_color.clear();

			acc_r += column[i].GetR();
			acc_g += column[i].GetG();
			acc_b += column[i].GetB();
			acc_n++;
			continue;
		}

		pending_index.push_back(i);
		pending_color.push_back(column[i]);

		if ((int)pending_index.size() >= ocr_color_split_run)
		{
			run_start.push_back(pending_index.front());

			acc_r = 0;
			acc_g = 0;
			acc_b = 0;
			for (const Gdiplus::Color& cr : pending_color)
			{
				acc_r += cr.GetR();
				acc_g += cr.GetG();
				acc_b += cr.GetB();
			}
			acc_n = (int)pending_color.size();

			pending_index.clear();
			pending_color.clear();
		}
	}

	//이웃 구간의 대표색이 가까우면 도로 합친다. 열 단위 판정은 획 모양에 흔들리므로,
	//마지막에 "구간 전체의 색" 으로 한 번 더 걸러야 진짜 색이 바뀐 곳만 남는다.
	if (run_start.size() > 1)
	{
		std::vector<int> merged;
		Gdiplus::Color prev = Gdiplus::Color::Transparent;

		for (size_t k = 0; k < run_start.size(); ++k)
		{
			const int from = run_start[k];
			const int to = (k + 1 < run_start.size()) ? run_start[k + 1] : rc.Width();

			CRect rc_run = rc;
			rc_run.left = rc.left + from;
			rc_run.right = rc.left + max(from + 1, to);
			if (rc_run.right > rc.right)
				rc_run.right = rc.right;

			const Gdiplus::Color cr = sample_ink_color(bgra, img_w, img_h, rc_run, whole.cr_back);

			if (!merged.empty() && color_distance(cr, prev) <= ocr_color_split_distance)
				continue;

			merged.push_back(from);
			prev = cr;
		}
		run_start = merged;
	}

	if (run_start.size() <= 1)
	{
		out_text.push_back(text);
		out_style.push_back(whole);
		return;
	}

	//글자를 구간에 배분한다 — 글자 i 의 중점이 어느 구간에 드는가.
	const double char_w = (double)rc.Width() / count;
	int begin = 0;

	while (begin < count)
	{
		const int mid = (int)((begin + 0.5) * char_w);
		int run = 0;
		for (size_t k = 0; k < run_start.size(); ++k)
		{
			if (mid >= run_start[k])
				run = (int)k;
		}

		int end = begin + 1;
		while (end < count)
		{
			const int mid_next = (int)((end + 0.5) * char_w);
			int run_next = 0;
			for (size_t k = 0; k < run_start.size(); ++k)
			{
				if (mid_next >= run_start[k])
					run_next = (int)k;
			}
			if (run_next != run)
				break;
			end++;
		}

		CRect rc_part = rc;
		rc_part.left  = rc.left + (int)(begin * char_w);
		rc_part.right = rc.left + (int)(end * char_w);
		if (rc_part.right > rc.right)
			rc_part.right = rc.right;

		//20260913 by claude. 배경도 조각마다 다시 뽑는다. 단어 전체의 배경을 물려주면, 한 단어 안에서
		//배경이 바뀌는 경우(형광 표시 등)를 놓친다 — "할아버지가" 에서 `할아` 에만 하늘색 형광이
		//깔려 있는데 전체 배경이 흰색으로 잡혀 그 형광이 통째로 사라졌다.
		//조각 바깥 링의 위/아래 띠가 링 픽셀의 대부분이라, 조각 단위로도 배경이 안정적으로 잡힌다
		//(실측: `할`/`아` = #9AC6E9, `버`/`지`/`가` = #F9F9F9).
		ocr_word_style style;
		style.pt = whole.pt;
		style.cr_back = (rc_part.Width() > 0)
			? sample_back_color(bgra, img_w, img_h, rc_part)
			: whole.cr_back;
		style.cr_text = (rc_part.Width() > 0)
			? sample_ink_color(bgra, img_w, img_h, rc_part, style.cr_back)
			: whole.cr_text;

		out_text.push_back(text.Mid(begin, end - begin));
		out_style.push_back(style);

		begin = end;
	}
}

//20260912 by claude. RTF 는 7-bit 스트림이다. 한글은 \uNNNN? 형태로만 안전하게 실린다.
static CString rtf_escape_text(const CString& text)
{
	CString out;
	for (int i = 0; i < text.GetLength(); ++i)
	{
		const TCHAR ch = text[i];
		if (ch == _T('\\') || ch == _T('{') || ch == _T('}'))
		{
			out += _T('\\');
			out += ch;
		}
		else if (ch < 0x80)
		{
			out += ch;
		}
		else
		{
			CString esc;
			esc.Format(_T("\\u%d?"), (int)(short)ch);
			out += esc;
		}
	}
	return out;
}

//20260912 by claude. 방향별 인식 결과를 한 줄에 모으는 단위. 색·크기는 그 방향의 버퍼에서 이미
//뽑아 두므로 여기엔 글자와 줄 번호만 남는다 (방향이 다르면 줄 번호가 겹치지 않게 밀어서 준다).
struct ocr_hit
{
	CString text;
	int     line = 0;
	bool    glued = false;  //앞 항목에 공백 없이 붙는다 (한 단어를 색으로 쪼갠 조각).
};

//20260913 by claude. 언샤프 마스크 (분리형 5-tap 가우시안 [1,4,6,4,1]/16, σ≈1.0).
//엔진은 흐릿한 획을 잘 못 읽는다 — 화면 캡처는 ClearType·동영상 압축·리샘플을 거치며 이미 뭉개져 있다.
//threshold 미만의 차이는 건드리지 않아 평탄한 배경에 노이즈를 만들지 않는다.
//확대 *전* 원본에 한 번만 건다. 확대 후에 거는 쪽은 실측에서 더 나빴다 (0.927 vs 0.901).
static void unsharp_bgra(std::vector<BYTE>& px, int w, int h, int percent, int threshold)
{
	if (px.size() < (size_t)w * h * 4 || w <= 0 || h <= 0)
		return;

	static const int kernel[5] = { 1, 4, 6, 4, 1 };
	static const int kernel_sum = 16;

	std::vector<BYTE> blurred(px.size());

	//가로
	for (int y = 0; y < h; ++y)
	{
		const BYTE* row = px.data() + (size_t)y * w * 4;
		BYTE* dst = blurred.data() + (size_t)y * w * 4;

		for (int x = 0; x < w; ++x)
		{
			int acc[3] = { 0, 0, 0 };
			for (int k = -2; k <= 2; ++k)
			{
				const int sx = min(max(x + k, 0), w - 1);
				const BYTE* p = row + (size_t)sx * 4;
				acc[0] += p[0] * kernel[k + 2];
				acc[1] += p[1] * kernel[k + 2];
				acc[2] += p[2] * kernel[k + 2];
			}
			BYTE* d = dst + (size_t)x * 4;
			d[0] = (BYTE)(acc[0] / kernel_sum);
			d[1] = (BYTE)(acc[1] / kernel_sum);
			d[2] = (BYTE)(acc[2] / kernel_sum);
			d[3] = row[(size_t)x * 4 + 3];
		}
	}

	//세로 + 합성
	std::vector<BYTE> column(px.size());
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			int acc[3] = { 0, 0, 0 };
			for (int k = -2; k <= 2; ++k)
			{
				const int sy = min(max(y + k, 0), h - 1);
				const BYTE* p = blurred.data() + ((size_t)sy * w + x) * 4;
				acc[0] += p[0] * kernel[k + 2];
				acc[1] += p[1] * kernel[k + 2];
				acc[2] += p[2] * kernel[k + 2];
			}
			BYTE* d = column.data() + ((size_t)y * w + x) * 4;
			d[0] = (BYTE)(acc[0] / kernel_sum);
			d[1] = (BYTE)(acc[1] / kernel_sum);
			d[2] = (BYTE)(acc[2] / kernel_sum);
		}
	}

	for (size_t i = 0; i + 3 < px.size(); i += 4)
	{
		for (int c = 0; c < 3; ++c)
		{
			const int diff = (int)px[i + c] - (int)column[i + c];
			if (diff < threshold && diff > -threshold)
				continue;

			int v = (int)px[i + c] + diff * percent / 100;
			px[i + c] = (BYTE)((v < 0) ? 0 : ((v > 255) ? 255 : v));
		}
	}
}

//20260913 by claude. 글자 bigram Dice 계수 (공백 무시). 후보들이 서로 얼마나 같은 말을 하는지 잰다.
//정답을 모르는 상태에서 "더 정확한 결과" 를 고르는 대용물이다 — 옳게 읽힌 결과는 여러 배율에서
//서로 비슷하게 나오고, 잘못 읽힌 결과는 제각각이다. 실측에서 difflib 과 동등했다 (0.927 vs 0.925).
static double text_similarity(const CString& a, const CString& b)
{
	auto build = [](const CString& s, std::map<std::wstring, int>& out) -> int
	{
		CString t;
		for (int i = 0; i < s.GetLength(); ++i)
		{
			if (!_istspace(s[i]))
				t += s[i];
		}

		int total = 0;
		if (t.GetLength() < 2)
		{
			if (t.GetLength() == 1)
			{
				out[std::wstring(1, t[0])]++;
				total = 1;
			}
			return total;
		}

		for (int i = 0; i + 1 < t.GetLength(); ++i)
		{
			const std::wstring key(t.GetString() + i, 2);
			out[key]++;
			total++;
		}
		return total;
	};

	std::map<std::wstring, int> ma;
	std::map<std::wstring, int> mb;
	const int na = build(a, ma);
	const int nb = build(b, mb);

	if (na == 0 || nb == 0)
		return (na == nb) ? 1.0 : 0.0;

	int inter = 0;
	for (const auto& kv : ma)
	{
		const auto it = mb.find(kv.first);
		if (it != mb.end())
			inter += min(kv.second, it->second);
	}
	return 2.0 * inter / (double)(na + nb);
}

//20260912 by claude. BGRA top-down 버퍼를 90도 단위로 돌린다. 직교 회전은 픽셀 이동일 뿐이라
//GDI+ 를 태우지 않는다 — 보간이 끼지 않아 원본 픽셀이 그대로 보존된다.
static void rotate_bgra(const BYTE* src, int w, int h, int angle, std::vector<BYTE>& dst, int& dw, int& dh)
{
	dw = h;
	dh = w;
	dst.resize((size_t)dw * dh * 4);

	for (int dy = 0; dy < dh; ++dy)
	{
		BYTE* row = dst.data() + (size_t)dy * dw * 4;
		for (int dx = 0; dx < dw; ++dx)
		{
			//90 = 시계방향. 원본 (x, y) 가 (h-1-y, x) 로 간다.
			const int sx = (angle == 90) ? dy : (w - 1 - dy);
			const int sy = (angle == 90) ? (h - 1 - dx) : dx;
			memcpy(row + (size_t)dx * 4, src + ((size_t)sy * w + sx) * 4, 4);
		}
	}
}

//20260912 by claude. 회전 패스에서 나오는 노이즈를 거른다. 엉뚱한 방향으로 읽으면 한글·영숫자가 아닌
//한자·괘선·희귀기호가 섞여 나온다 (실측: "1러Y8|( 4고께z 目들 日十07"). 정상 인식은 거의 전부
//한글/영숫자/일반 문장부호다. 0도 결과에는 적용하지 않는다 — 그건 그대로 믿는다.
static bool looks_like_text(const CString& text)
{
	if (text.IsEmpty())
		return false;

	int good = 0;
	for (int i = 0; i < text.GetLength(); ++i)
	{
		const wchar_t ch = text[i];
		const bool hangul = (ch >= 0xAC00 && ch <= 0xD7A3) || (ch >= 0x3130 && ch <= 0x318F);
		const bool alnum  = (ch >= 0x30 && ch <= 0x39) || (ch >= 0x41 && ch <= 0x5A) || (ch >= 0x61 && ch <= 0x7A);
		const bool punct  = (ch < 0x80) && (wcschr(L" .,!?:;%()[]/-+=~#&*_", ch) != NULL);
		if (hangul || alnum || punct)
			good++;
	}
	//[가정] 정상 인식은 한글/영숫자/문장부호가 80% 이상이다. 한자·일본어가 섞인 정상 텍스트를
	//노이즈로 오판할 수 있다. 방향·배율을 고르는 점수에만 쓰고 최종 출력은 거르지 않으므로,
	//틀리면 "덜 좋은 방향을 고르는" 정도로 끝난다.
	return (good * 5 >= text.GetLength() * 4);
}

//20260913 by claude. 그 단어가 "내용" 을 담고 있나 — 한글이나 영숫자가 하나라도 있어야 한다.
//문장부호·기호만으로 된 단어는 대개 엔진이 잡티를 글자로 오인한 것이다
//(실측: 2x14 픽셀짜리 ',', 8x3 픽셀짜리 '~'). 이런 것에 점수를 주면 안 된다.
static bool has_content(const CString& text)
{
	for (int i = 0; i < text.GetLength(); ++i)
	{
		const wchar_t ch = text[i];
		if (ch >= 0xAC00 && ch <= 0xD7A3)
			return true;
		if (ch >= 0x30 && ch <= 0x39)
			return true;
		if ((ch >= 0x41 && ch <= 0x5A) || (ch >= 0x61 && ch <= 0x7A))
			return true;
	}
	return false;
}

//20260912 by claude. 단어별 색·크기를 그대로 담은 RTF. 클립보드의 "Rich Text Format" 포맷으로 나간다.
//글자색은 \cf, 배경색은 \highlight 로 싣는다 — 어두운 테마 화면을 캡처했을 때 흰 글자가
//흰 문서 배경에 묻히지 않게 하려면 배경도 같이 실어야 한다.
//20260912 by claude. 단어들이 깔고 있던 배경 중 최빈색. 토스트 패널을 이 색으로 깔면
//원본에서 이미 확보돼 있던 글자-배경 대비가 그대로 재현된다.
//
//[가정] 한 이미지의 배경은 대체로 한 가지다. 어두운 헤더 + 흰 본문처럼 섞이면 한쪽만 맞는다.
//다만 build_tagged 의 get_readable_text_color 대비 보정이 뒤를 받쳐 가독성 자체는 유지된다.
static Gdiplus::Color get_dominant_back_color(const std::vector<ocr_word_style>& styles)
{
	auto quantize = [](Gdiplus::Color cr) -> DWORD
	{
		return ((DWORD)(cr.GetR() >> 3) << 10) | ((DWORD)(cr.GetG() >> 3) << 5) | (DWORD)(cr.GetB() >> 3);
	};

	std::map<DWORD, int> hist;
	for (const ocr_word_style& style : styles)
		hist[quantize(style.cr_back)]++;

	DWORD best_key = 0;
	int best_count = -1;
	for (const auto& kv : hist)
	{
		if (kv.second > best_count)
		{
			best_count = kv.second;
			best_key = kv.first;
		}
	}

	//최빈 버킷에 속한 실제 색들의 평균 — 양자화로 뭉갠 값을 되돌린다.
	int count = 0;
	int r = 0;
	int g = 0;
	int b = 0;
	for (const ocr_word_style& style : styles)
	{
		if (quantize(style.cr_back) != best_key)
			continue;
		count++;
		r += style.cr_back.GetR();
		g += style.cr_back.GetG();
		b += style.cr_back.GetB();
	}
	if (count == 0)
		return toast_back_default;

	//패널은 살짝 비쳐야 "떠 있는 창" 으로 읽힌다. 알파는 기본 패널과 같게 둔다.
	return Gdiplus::Color(toast_back_default.GetA(), (BYTE)(r / count), (BYTE)(g / count), (BYTE)(b / count));
}

static CString build_rtf(const std::vector<ocr_hit>& hits, const std::vector<ocr_word_style>& styles)
{
	std::vector<DWORD> palette;
	auto color_index = [&palette](Gdiplus::Color cr) -> int
	{
		const DWORD key = ((DWORD)cr.GetR() << 16) | ((DWORD)cr.GetG() << 8) | (DWORD)cr.GetB();
		for (size_t i = 0; i < palette.size(); ++i)
		{
			if (palette[i] == key)
				return (int)i + 1;
		}
		palette.push_back(key);
		return (int)palette.size();
	};

	CString body;
	int cur_line = -1;

	for (size_t i = 0; i < hits.size(); ++i)
	{
		if (hits[i].line != cur_line)
		{
			if (cur_line >= 0)
				body += _T("\\par\r\n");
			cur_line = hits[i].line;
		}
		else if (!hits[i].glued)
		{
			body += _T(" ");
		}

		CString run;
		run.Format(_T("\\cf%d\\highlight%d\\fs%d "),
			color_index(styles[i].cr_text),
			color_index(styles[i].cr_back),
			max(2, (int)(styles[i].pt * 2.0f + 0.5f)));
		body += run;
		body += rtf_escape_text(hits[i].text);
	}

	CString colortbl = _T("{\\colortbl ;");
	for (size_t i = 0; i < palette.size(); ++i)
	{
		CString one;
		one.Format(_T("\\red%d\\green%d\\blue%d;"),
			(int)((palette[i] >> 16) & 0xFF), (int)((palette[i] >> 8) & 0xFF), (int)(palette[i] & 0xFF));
		colortbl += one;
	}
	colortbl += _T("}");

	return CString(_T("{\\rtf1\\ansi\\ansicpg949\\deff0\\uc1\r\n"))
		+ _T("{\\fonttbl{\\f0\\fnil\\fcharset129 Malgun Gothic;}}\r\n")
		+ colortbl + _T("\r\n\\viewkind4\\pard\\f0\r\n")
		+ body + _T("\r\n}");
}

//20260912 by claude. 평문. 여러 방향의 결과를 합쳤을 수 있으므로 엔진이 준 result.text 를 쓰지 않고 직접 만든다.
static CString build_plain(const std::vector<ocr_hit>& hits)
{
	CString out;
	int cur_line = -1;

	for (size_t i = 0; i < hits.size(); ++i)
	{
		if (hits[i].line != cur_line)
		{
			if (cur_line >= 0)
				out += _T("\r\n");
			cur_line = hits[i].line;
		}
		else if (!hits[i].glued)
		{
			out += _T(" ");
		}
		out += hits[i].text;
	}
	return out;
}

//20260912 by claude. 토스트용 — SCParagraph 서식 태그로 같은 색·크기를 재현한다.
//토스트는 원본 배경이 아니라 패널 한 장 위에 글자를 얹는다. 글자색만 원본에서
//가져오면 패널과 밝기가 비슷할 때 글자가 보이지 않는다 — 실제로 그 사고가 났다.
//얹을 배경(cr_panel)에 대해 get_readable_text_color 로 확인해서, WCAG 4.5 를 넘으면 원본 색 그대로 두고
//못 넘을 때만 흑/백 중 대비가 높은 쪽으로 바꾼다. 색을 최대한 살리면서 가독성은 보장된다.
static CString build_tagged(const std::vector<ocr_hit>& hits, const std::vector<ocr_word_style>& styles,
							Gdiplus::Color cr_panel)
{
	CString out;
	int cur_line = -1;

	for (size_t i = 0; i < hits.size(); ++i)
	{
		if (hits[i].line != cur_line)
		{
			if (cur_line >= 0)
				out += _T("<br>");
			cur_line = hits[i].line;
		}
		else if (!hits[i].glued)
		{
			out += _T(" ");
		}

		//20260913 by claude. 그 단어가 패널과 다른 배경을 깔고 있으면 그 배경을 같이 그린다 —
		//`할아버지가` 의 `할아` 처럼 형광이 깔린 구간이 토스트에서만 사라지던 문제.
		//패널과 같은 색이면 태그를 내지 않는다. run 박스는 불투명이라, 반투명 패널 위에 같은 색을
		//덧칠하면 그 단어만 더 진해 보여 얼룩이 된다.
		const bool own_back = (color_distance(styles[i].cr_back, cr_panel) > ocr_color_split_distance);

		//글자색은 실제로 깔릴 배경에 대해 읽히는지 본다. 배경을 같이 그릴 때는 그 배경이 기준이다.
		const Gdiplus::Color cr_under = own_back ? styles[i].cr_back : cr_panel;
		const Gdiplus::Color cr_text = get_readable_text_color(cr_under, styles[i].cr_text);

		if (own_back)
		{
			CString back_tag;
			back_tag.Format(_T("<crb=#%02X%02X%02X>"),
				styles[i].cr_back.GetR(), styles[i].cr_back.GetG(), styles[i].cr_back.GetB());
			out += back_tag;
		}

		CString tag;
		tag.Format(_T("<cr=#%02X%02X%02X><sz=%d>"),
			cr_text.GetR(), cr_text.GetG(), cr_text.GetB(),
			max(1, (int)(styles[i].pt + 0.5f)));
		out += tag;

		CString word = hits[i].text;
		word.Replace(_T("&"), _T("&amp;"));
		word.Replace(_T("<"), _T("&lt;"));
		word.Replace(_T(">"), _T("&gt;"));
		out += word;

		if (own_back)
			out += _T("</crb>");
	}
	return out;
}

//20260912 by claude. 그 방향에서 "글자답게" 읽힌 양. 방향·배율 판정 점수로 쓴다.
//단어 수가 아니라 글자 수로 세는 이유는, 엉뚱한 방향이 짧은 쓰레기 단어를 여럿 뱉는 일이 잦아서다.
//
//[가정, 이 코드에서 가장 약한 고리] 이 점수는 읽힌 **양** 을 잴 뿐 **정확도** 를 재지 않는다.
//쓰레기를 많이 뱉는 방향·배율이 이길 수 있다. 정답을 모르는 상태에서 "더 정확한 결과" 를 재는
//방법을 찾지 못해 수량으로 대신하고 있다. 더 나은 척도가 생기면 여기를 바꾼다.
static int score_orientation(const SCOcrResult& result)
{
	int score = 0;
	for (const SCOcrWord& word : result.words)
	{
		const CString text = word.text.c_str();

		//문장부호만 있는 단어는 글자 수만 늘릴 뿐 내용이 없다. 세면 잡티가 많은 쪽이 이긴다 —
		//실제로 '1054007' 을 정확히 읽은 배율들을 제치고, 쉼표 잡티 하나가 더 붙어 8자가 된
		//배율이 채택되어 ', A054007' 이 나왔다.
		if (!has_content(text))
			continue;

		if (looks_like_text(text))
			score += text.GetLength();
	}
	return score;
}

//20260913 by claude. 한 방향에 대해 배율 후보를 전부 인식한 뒤, **다른 후보들과 가장 많이 일치하는**
//결과를 고른다. 이전에는 "글자다운 문자 수가 가장 많은 것" 을 골랐는데 그게 틀렸다 — 그건 양을 잴 뿐
//정확도를 재지 않는다. 실제로 `1054007` 을 정확히 읽은 배율들을 제치고, 2x14 픽셀짜리 쉼표 잡티가
//하나 더 붙어 8자가 된 배율이 채택되어 `, A054007` 이 나왔다.
//합의로 바꾸면 정답셋 16장 평균이 0.866 -> 0.927, 완전일치 4장 -> 8장이 된다.
//probe 는 x1 결과 — 방향을 고르느라 이미 한 번 인식했으므로 다시 돌리지 않는다.
static SCOcrResult recognize_best_scale(Gdiplus::Bitmap& bmp, int w, int h, int angle,
										const SCOcrResult& probe, double& scale)
{
	struct candidate
	{
		double      scale = 1.0;
		SCOcrResult result;
		CString     text;
	};

	std::vector<candidate> list;

	{
		candidate first;
		first.scale = 1.0;
		first.result = probe;
		first.text = probe.text.c_str();
		list.push_back(first);
	}

	for (const double factor : ocr_scale_candidates)
	{
		if (fabs(factor - 1.0) <= 0.05)
			continue;

		const int nw = (int)(w * factor + 0.5);
		const int nh = (int)(h * factor + 0.5);

		if (nw <= 0 || nh <= 0)
			continue;
		if (max(nw, nh) > ocr_max_image_dimension || (double)nw * nh > ocr_max_pixels)
		{
			logWrite(_T("[OCR] %d도 배율 x%.2f 건너뜀 — %dx%d 는 한도 초과"), angle, factor, nw, nh);
			continue;
		}

		//MFC 디버그 빌드의 #define new DEBUG_NEW 가 GdiplusBase::operator new 와 충돌한다.
		//인식이 끝나면 버릴 비트맵이라 힙에 둘 이유도 없다 — 스택에 만든다.
		Gdiplus::Bitmap resampled(nw, nh, PixelFormat32bppARGB);

		Gdiplus::Graphics g(&resampled);
		g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
		g.DrawImage(&bmp, 0, 0, nw, nh);

		candidate one;
		one.scale = factor;
		one.result = sc_win_ocr_ex(&resampled);
		if (!one.result.error.empty())
			continue;

		one.text = one.result.text.c_str();
		list.push_back(one);
	}

	//다른 후보들과의 평균 유사도가 가장 높은 것. 동점이면 먼저 나온 쪽(= 더 작은 배율) — 재샘플링은 공짜가 아니다.
	int best = 0;
	double best_agreement = -1.0;

	for (size_t i = 0; i < list.size(); ++i)
	{
		double total = 0.0;
		int count = 0;

		for (size_t k = 0; k < list.size(); ++k)
		{
			if (k == i)
				continue;
			total += text_similarity(list[i].text, list[k].text);
			count++;
		}

		const double agreement = (count > 0) ? (total / count) : 1.0;
		logWrite(_T("[OCR] %d도 배율 x%.2f — 단어 %d개, 합의도 %.3f, %dms"),
			angle, list[i].scale, list[i].result.word_count, agreement, list[i].result.elapsed_ms);

		if (agreement > best_agreement + 1e-9)
		{
			best_agreement = agreement;
			best = (int)i;
		}
	}

	scale = list[best].scale;
	logWrite(_T("[OCR] %d도 채택 배율 x%.2f (합의도 %.3f)"), angle, scale, best_agreement);
	return list[best].result;
}

void CSCDeskToolsDlg::run_ocr_on_bgra(const BYTE* bgra_top_down, int w, int h, CWnd* center_on)
{
	//20260912 by claude. 캡처 노트의 "텍스트 인식" 과 전역 단축키가 함께 타는 단일 경로.
	//이미지는 이미 클립보드에 있으므로 여기서는 인식한 텍스트를 RTF(색·크기 포함) + 평문으로 넣는다.
	if (!bgra_top_down || w <= 0 || h <= 0)
		return;

	CWnd* anchor = (center_on && ::IsWindow(center_on->GetSafeHwnd())) ? center_on : this;
	const UINT dpi = win_compat::dpi::for_window(anchor->GetSafeHwnd());
	logWrite(_T("[OCR] 시작 — 이미지 %d x %d, 기준 DPI %d"), w, h, dpi);

	//20260912 by claude. [진단] 엔진에 넘긴 픽셀을 그대로 파일로 남긴다. 인식이 나쁠 때
	//"앱이 엉뚱한 픽셀을 넘긴 것"인지 "제대로 넘겼는데 엔진이 못 읽은 것"인지는 이 파일을 봐야 갈린다.
	{
		CString folder = get_exe_directory() + _T("\\Log");
		::CreateDirectory(folder, NULL);

		SYSTEMTIME st;
		::GetLocalTime(&st);

		CString path;
		path.Format(_T("%s\\ocr_input_%04d%02d%02d_%02d%02d%02d.png"),
			folder.GetString(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

		//BitBlt 로 뜬 캡처는 alpha 가 0 이다. 32bppRGB 는 4번째 바이트를 무시하므로 불투명으로 전달된다.
		Gdiplus::Bitmap src(w, h, w * 4, PixelFormat32bppRGB, const_cast<BYTE*>(bgra_top_down));
		CSCGdiplusBitmap dump(&src);
		logWrite(_T("[OCR] 입력 덤프 saved=%d '%s'"), dump.save(path) ? 1 : 0, path.GetString());
	}

	//20260912 by claude. sc_win_ocr_ex 는 내부 MTA 스레드를 join 하므로 그동안 UI 가 멈춘다.
	CWaitCursor wait;

	//20260913 by claude. 인식에 넘기는 픽셀은 선명화한 것이고, 색을 뽑는 픽셀은 원본이다.
	//선명화는 획 대비를 올려 인식을 돕지만 가장자리 색을 과장하므로, 그걸로 색을 뽑으면 원래 색과 달라진다.
	//회전은 픽셀 이동일 뿐이라 "선명화 후 회전" 과 "회전 후 선명화" 가 같다 — 그래서 한 번만 건다.
	std::vector<BYTE> sharpened(bgra_top_down, bgra_top_down + (size_t)w * h * 4);
	unsharp_bgra(sharpened, w, h, ocr_unsharp_percent, ocr_unsharp_threshold);

	//엔진에는 자동 회전 보정이 없다 — 실측에서 책등의 세로 글자가 0도에서는 한 글자도 안 나오다가
	//90도로 돌리자 전부 나왔다. 그래서 세 방향을 재 보고 가장 글자답게 읽힌 방향을 고른다.
	const int angles[] = { 0, 90, 270 };

	std::vector<BYTE> rotated_sharp;
	std::vector<BYTE> rotated_plain;
	int best_angle = 0;
	int best_score = -1;
	SCOcrResult best_probe;

	for (int i = 0; i < 3; ++i)
	{
		const int angle = angles[i];

		std::vector<BYTE> buffer;
		const BYTE* src = sharpened.data();
		int sw = w;
		int sh = h;

		if (angle != 0)
		{
			rotate_bgra(sharpened.data(), w, h, angle, buffer, sw, sh);
			src = buffer.data();
		}

		Gdiplus::Bitmap bmp(sw, sh, sw * 4, PixelFormat32bppRGB, const_cast<BYTE*>(src));
		const SCOcrResult probe = sc_win_ocr_ex(&bmp);

		if (angle == 0 && !probe.error.empty())
		{
			logWriteE(_T("[OCR] 실패 — %s"), probe.error.c_str());

			CString message;
			message.Format(_T("텍스트 인식에 실패했습니다.\n\n%s"), probe.error.c_str());
			AfxMessageBox(message, MB_ICONWARNING);
			return;
		}

		const int score = score_orientation(probe);
		logWrite(_T("[OCR] 방향 탐색 %d도 — 단어 %d개, 글자다운 문자 %d자, %dms"),
			angle, probe.word_count, score, probe.elapsed_ms);

		//동점이면 0도를 유지한다. 세로로 읽어도 비슷하게 나오는 이미지는 원래 방향이 맞을 확률이 높다.
		if (score > best_score)
		{
			best_score = score;
			best_angle = angle;
			best_probe = probe;
		}
	}

	//인식용(선명화)과 색 추출용(원본)을 같은 방향으로 각각 준비한다.
	const BYTE* src_sharp = sharpened.data();
	const BYTE* src = bgra_top_down;
	int sw = w;
	int sh = h;

	if (best_angle != 0)
	{
		rotate_bgra(sharpened.data(), w, h, best_angle, rotated_sharp, sw, sh);
		rotate_bgra(bgra_top_down, w, h, best_angle, rotated_plain, sw, sh);
		src_sharp = rotated_sharp.data();
		src = rotated_plain.data();
	}
	logWrite(_T("[OCR] 채택 방향 %d도 (%d자)"), best_angle, best_score);

	Gdiplus::Bitmap bmp(sw, sh, sw * 4, PixelFormat32bppRGB, const_cast<BYTE*>(src_sharp));

	double scale = 1.0;
	const SCOcrResult result = recognize_best_scale(bmp, sw, sh, best_angle, best_probe, scale);

	std::vector<ocr_hit> hits;
	std::vector<ocr_word_style> styles;

	const double inv = (scale > 0.0) ? (1.0 / scale) : 1.0;

	for (const SCOcrWord& word : result.words)
	{
		//정규화했다면 좌표가 확대·축소된 이미지 기준이다. 색은 그 방향 버퍼의 원래 해상도에서 뽑는다.
		const CRect rc((int)(word.x * inv), (int)(word.y * inv),
			(int)((word.x + word.width) * inv), (int)((word.y + word.height) * inv));
		const int height_src = (int)(word.height * inv + 0.5);

		//한 단어 안에서 색이 바뀌면 쪼갠다 — 엔진은 단어 단위로만 돌려주기 때문이다.
		std::vector<CString> parts;
		std::vector<ocr_word_style> part_styles;
		split_word_by_color(src, sw, sh, rc, dpi, height_src, CString(word.text.c_str()), parts, part_styles);

		for (size_t k = 0; k < parts.size(); ++k)
		{
			ocr_hit hit;
			hit.text  = parts[k];
			hit.line  = word.line;
			hit.glued = (k > 0);

			hits.push_back(hit);
			styles.push_back(part_styles[k]);

			logWrite(_T("[OCR] word line=%d rect=(%d,%d,%d,%d) 글자=#%02X%02X%02X 배경=#%02X%02X%02X %.1fpt '%s'%s"),
				hit.line, rc.left, rc.top, rc.Width(), rc.Height(),
				part_styles[k].cr_text.GetR(), part_styles[k].cr_text.GetG(), part_styles[k].cr_text.GetB(),
				part_styles[k].cr_back.GetR(), part_styles[k].cr_back.GetG(), part_styles[k].cr_back.GetB(),
				part_styles[k].pt, hit.text.GetString(),
				(parts.size() > 1) ? _T(" [색 분할]") : _T(""));
		}
	}

	if (hits.empty())
	{
		logWriteW(_T("[OCR] 인식된 텍스트 없음"));
		show_message(_T("인식된 텍스트가 없습니다."), anchor);
		return;
	}

	const CString plain = build_plain(hits);
	const CString rtf = build_rtf(hits, styles);
	logWrite(_T("[OCR] 단어 %d개, RTF %d 자, 평문 %d 자"), (int)hits.size(), rtf.GetLength(), plain.GetLength());

	//RTF(서식) + 평문을 같은 클립보드 세션에 담는다. 워드/한글은 RTF 를, 메모장은 평문을 가져간다.
	if (::OpenClipboard(m_hWnd))
	{
		::EmptyClipboard();

		//"Rich Text Format" 은 7-bit ANSI 스트림이다 (한글은 rtf_escape_text 가 \\uNNNN? 로 바꿔 둠).
		const CStringA rtf_ansi(rtf);
		const size_t rtf_bytes = (size_t)rtf_ansi.GetLength() + 1;
		HGLOBAL hg_rtf = ::GlobalAlloc(GMEM_MOVEABLE, rtf_bytes);
		if (hg_rtf)
		{
			memcpy(::GlobalLock(hg_rtf), (LPCSTR)rtf_ansi, rtf_bytes);
			::GlobalUnlock(hg_rtf);

			const UINT cf_rtf = ::RegisterClipboardFormat(_T("Rich Text Format"));
			if (cf_rtf == 0 || ::SetClipboardData(cf_rtf, hg_rtf) == NULL)
				::GlobalFree(hg_rtf);
		}

		const size_t text_bytes = ((size_t)plain.GetLength() + 1) * sizeof(TCHAR);
		HGLOBAL hg_text = ::GlobalAlloc(GMEM_MOVEABLE, text_bytes);
		if (hg_text)
		{
			memcpy(::GlobalLock(hg_text), plain.GetString(), text_bytes);
			::GlobalUnlock(hg_text);

			if (::SetClipboardData(CF_UNICODETEXT, hg_text) == NULL)
				::GlobalFree(hg_text);
		}

		::CloseClipboard();
	}
	else
	{
		logWriteE(_T("[OCR] OpenClipboard 실패 (%d)"), ::GetLastError());
	}

	const Gdiplus::Color cr_panel = get_dominant_back_color(styles);
	logWrite(_T("[OCR] 토스트 패널색 #%02X%02X%02X"), cr_panel.GetR(), cr_panel.GetG(), cr_panel.GetB());

	//20260913 by claude. [진단] 토스트로 넘어가는 태그 문자열 그대로. 추출값은 맞는데 화면에만
	//안 나오는 경우, 태그가 안 붙은 것인지 붙었는데 안 그려지는 것인지는 이걸 봐야 갈린다.
	const CString tagged = build_tagged(hits, styles, cr_panel);
	logWrite(_T("[OCR] 토스트 태그: %s"), tagged.GetString());

	show_message_tagged(tagged, anchor, cr_panel);
}

void CSCDeskToolsDlg::show_message(CString message, CWnd* center_on)
{
	//20260912 by claude. SCParagraph 는 <...> 를 태그로 읽는다. OCR 결과처럼 임의의 문자열을 그대로 넘기면
	//<div> 같은 조각이 통째로 사라지거나 엉뚱한 서식이 걸린다. &amp; 를 먼저 바꿔야 이중 변환이 없다.
	message.Replace(_T("&"), _T("&amp;"));
	message.Replace(_T("<"), _T("&lt;"));
	message.Replace(_T(">"), _T("&gt;"));

	show_message_tagged(message, center_on);
}

void CSCDeskToolsDlg::show_message_tagged(CString tagged, CWnd* center_on, Gdiplus::Color cr_panel)
{
	//알파 0 = "패널색 지정 없음". 기본 어두운 패널을 쓴다.
	m_message.get_text_setting()->text_prop.cr_back = (cr_panel.GetA() == 0) ? toast_back_default : cr_panel;

	CWnd* anchor = (center_on && ::IsWindow(center_on->GetSafeHwnd())) ? center_on : this;

	const CRect rc_monitor = get_monitor_rect(get_monitor_index(anchor->GetSafeHwnd()));

	//이 창은 DPI 를 따라 커져야 하는 UI 다 (§4.6). m_dpi 가 아니라 지금 띄울 모니터의 DPI 로 환산한다 —
	//메인 다이얼로그는 숨어 있거나 다른 모니터에 있을 수 있다.
	const UINT dpi = monitor_dpi_for_window(anchor->GetSafeHwnd());
	m_message.get_text_setting()->text_prop.size = win_compat::dpi::scale_f(20.0f, dpi);
	m_message.set_round(win_compat::dpi::scale_f(12.0f, dpi), false);
	m_message.set_margin(win_compat::dpi::scale_f(16.0f, dpi), false);

	//한 줄이 화면을 가로지르면 읽기 어렵다. 기준 모니터 폭의 절반에서 줄바꿈시킨다.
	m_message.set_max_width(rc_monitor.Width() / 2);

	m_message.set_text(tagged);
	m_message.CenterWindow(anchor);

	//캡처 노트가 WS_EX_TOPMOST 라 그냥 띄우면 그 아래로 들어가 가려진다.
	m_message.SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	m_message.fade_in(0, 1000, true);
}

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

//20260912 by claude. 클립보드의 이미지를 top-down 32bpp BGRA 로 읽는다.
//노트로 띄우기(Alt+Shift+V)와 텍스트 인식(Alt+Shift+T)이 같은 입력을 봐야 하므로 한 곳에 둔다.
bool CSCDeskToolsDlg::read_clipboard_image_bgra(std::vector<BYTE>& bgra, int& width, int& height)
{
	width = 0;
	height = 0;
	bgra.clear();

	if (!::OpenClipboard(m_hWnd))
	{
		AfxMessageBox(_T("클립보드 열기 실패"));
		return false;
	}

	bool ok = false;

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

	return ok;
}

void CSCDeskToolsDlg::OnToolPasteClipboard()
{
	//클립보드의 이미지를 읽어 floating note 로 띄운다.
	//테스트용으로 매번 캡처할 필요 없게 한다 — 다른 앱에서 Ctrl+C 한 이미지를 붙여넣기.
	std::vector<BYTE> bgra;
	int width = 0;
	int height = 0;

	if (!read_clipboard_image_bgra(bgra, width, height))
	{
		AfxMessageBox(_T("클립보드에 이미지가 없거나 지원되지 않는 형식입니다.\n(24/32bpp DIB 만 지원)"));
		return;
	}

	CSCCapturedNoteDlg::spawn(bgra.data(), width, height, NULL);
}

//20260912 by claude. 전역 단축키(Alt+Shift+T) — 캡처 노트를 띄우지 않는 설정에서도 인식할 수 있어야 한다.
//캡처하면 이미지가 곧바로 클립보드에 들어가므로, 그 마지막 이미지를 대상으로 삼는다.
//인식 이후(클립보드 기록·floating 표시)는 노트의 Ctrl+T 와 완전히 같은 경로다.
void CSCDeskToolsDlg::OnToolOcrClipboard()
{
	std::vector<BYTE> bgra;
	int width = 0;
	int height = 0;

	if (!read_clipboard_image_bgra(bgra, width, height))
	{
		logWriteW(_T("[OCR] 클립보드에 인식할 이미지가 없음"));
		show_message(_T("클립보드에 이미지가 없습니다."), NULL);
		return;
	}

	//center_on 은 주지 않는다 — 노트가 없는 상황이라 메인 다이얼로그(숨어 있어도 위치는 유효) 기준이 된다.
	run_ocr_on_bgra(bgra.data(), width, height, NULL);
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
