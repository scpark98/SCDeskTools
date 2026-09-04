
// SCDeskToolsDlg.h: 헤더 파일
//

#pragma once

#include <vector>
#include <memory>

#include "Common/system/SysTrayIcon/SysTrayIcon.h"
#include "Common/CDialog/CSCColorPicker/SCColorPicker.h"

// CSCDeskToolsDlg 대화 상자
class CSCDeskToolsDlg : public CDialogEx
{
// 생성입니다.
public:
	CSCDeskToolsDlg(CWnd* pParent = nullptr);	// 표준 생성자입니다.

// 대화 상자 데이터입니다.
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_SCDESKTOOLS_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 지원입니다.


// 구현입니다.
protected:
	HICON m_hIcon;

	CSysTrayIcon	m_sys_tray;
	CSCColorPicker	m_color_picker;	//modeless 컬러 피커. OnInitDialog 에서 1회 create.

	//메인 창에 노출할 즐겨찾기 툴 ID 목록. 향후 설정창에서 편집 → 레지스트리 저장 예정.
	//지금은 OnInitDialog 에서 default_favorites 로 초기화.
	std::vector<UINT>							m_favorites;
	std::vector<std::unique_ptr<CButton>>		m_buttons_favorite;	//m_favorites 와 1:1 매핑, 동적 생성.

	//우측 하단 종료 버튼. 현재는 숨김 (build_exit_button 의 Create 스타일에 WS_VISIBLE 없음).
	//트레이 종료 절차를 거치기 번거로울 때 도로 노출하면 한 번 클릭으로 종료 가능.
	CButton			m_button_exit;

	//캡처 시 floating note 를 띄우지 않고 클립보드에만 저장.
	//상태는 레지스트리(settings\capture_clipboard_only) 에 저장 → 재시작 후에도 복원.
	CButton			m_check_clipboard_only;
	bool			m_clipboard_only = false;
	enum : UINT { id_check_clipboard_only = 2001 };

	void			build_buttons();
	void			build_exit_button();
	void			build_clipboard_only_checkbox();

	//창 크기 · 폰트 · 자식 배치의 단일 기준. OnInitDialog 에서는 모니터에서, WM_DPICHANGED 에서는
	//메시지의 wParam 에서 받는다 (그 시점엔 창이 아직 옛 모니터에 걸쳐 있을 수 있다).
	UINT			m_dpi = 96;

	//96 DPI 기준으로 적은 레이아웃 픽셀을 m_dpi 로 환산한다.
	//Per-Monitor V2 라 다이얼로그 폰트는 OS 가 만들어 주지만 코드에 적은 크기는 따라가지 않는다.
	int				scaled(int px_at_96dpi) const;

	//m_dpi 하나로부터 창 크기 · 폰트 · 자식 배치를 전부 다시 만든다. 시작할 때와 모니터를 옮겼을 때가
	//같은 코드를 타야 두 경로가 어긋나지 않는다. pos_hint = WM_DPICHANGED 권장 rect (위치로만 사용).
	void			apply_dpi_layout(const RECT* pos_hint);

	//런타임 생성 자식들이 쓰는 폰트. 다이얼로그 폰트(GetFont)를 그대로 쓰면 모니터를 옮겼을 때
	//배치는 새 DPI 로 다시 계산되는데 폰트만 이전 배율로 남아 글자가 버튼을 넘친다.
	//시작 시점의 폰트/DPI 를 기준으로 잡아 두고 현재 DPI 비율로 다시 만든다.
	CFont			m_font_ui;
	LOGFONT			m_lf_base = {};
	UINT			m_dpi_base = 96;
	void			rebuild_ui_font();

	//배율이 다른 모니터로 창을 옮기면 OS 가 보내준다. scaled() 로 배치한 자식들은 따라가지 않으므로 다시 만든다.
	afx_msg LRESULT	on_dpi_changed(WPARAM wParam, LPARAM lParam);
	void			register_global_hotkeys();
	void			unregister_global_hotkeys();
	//모니터별 캡처 단축키는 동적 — 시작 시 + WM_DISPLAYCHANGE 시 재등록.
	void			register_monitor_hotkeys();
	void			unregister_monitor_hotkeys();
	int				m_registered_monitor_count = 0;	//현재 등록된 모니터 단축키 개수
	void			send_image_to_clipboard_and_note(const BYTE* bgra_top_down, int w, int h, POINT note_pos);
	void			capture_screen_rect(const CRect& rc_screen);
	void			show_tools_popup_menu(CPoint pt_screen);
	void			toggle_main_window();
	bool			clipboard_has_image() const;
	void			update_paste_clipboard_state();

	//전체화면 오버레이 띄우기 전 메인/피커 숨김 → RAII destructor 가 복원.
	//4개 OnTool*() 핸들러가 모두 같은 hide → wait → overlay → restore 패턴이라 추출.
	struct HideFloating
	{
		CSCDeskToolsDlg*	dlg = nullptr;
		bool				main_was_visible = false;
		bool				picker_was_visible = false;

		HideFloating(CSCDeskToolsDlg* d);
		~HideFloating();
	};

	// 생성된 메시지 맵 함수
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg LRESULT on_message_CSysTrayIcon(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT on_message_CSCColorPicker(WPARAM wParam, LPARAM lParam);
	afx_msg void OnToolColorPicker();
	afx_msg void OnToolDropper();
	afx_msg void OnToolCaptureWindow();
	afx_msg void OnToolCaptureRegion();
	afx_msg void OnToolCaptureFreehand();
	afx_msg void OnToolCaptureFullscreen();
	afx_msg void OnToolCaptureMonitor(UINT nID);	//ID_TOOL_CAPTURE_MONITOR_FIRST..LAST 범위 처리
	afx_msg void OnToolPasteClipboard();
	afx_msg void OnToolProtractor();
	afx_msg void OnToolRuler();
	afx_msg void OnAppShowHide();
	afx_msg void OnAppAbout();
	afx_msg void OnAppExit();

	// 시작프로그램으로 실행 시 Shell_TrayWnd 가 뒤늦게 올라오는 경우 복구.
	// Explorer 크래시 후 재시작에도 동일하게 사용.
	static UINT Message_TaskbarCreated;
	afx_msg LRESULT on_message_TaskbarCreated(WPARAM wParam, LPARAM lParam);

	//시작 시 메인 창 숨김 — OnInitDialog 끝에서 PostMessage 로 던져 첫 페인트 직후 처리.
	enum : UINT { Message_HideOnStartup = WM_USER + 100 };

	//20260904 by claude. WM_DPICHANGED 핸들러 *안에서* 창 크기를 바꾸면 소용이 없다 —
	//창을 옮기는 동작이 그 메시지보다 나중에 끝나면서 변경 전 크기를 도로 덮어쓴다.
	//그래서 레이아웃 적용을 이 메시지로 미룬다.
	//단 마우스 드래그는 modal move loop 안에서 돌기 때문에 이 메시지도 *루프 안에서* 처리되어
	//똑같이 덮어써진다. 그래서 드래그 중이면 여기서 하지 않고 WM_EXITSIZEMOVE 까지 더 미룬다.
	enum : UINT { Message_ApplyDpiLayout = WM_USER + 101 };
	afx_msg LRESULT	on_message_ApplyDpiLayout(WPARAM wParam, LPARAM lParam);

	bool			m_in_size_move = false;			//마우스로 이동 / 크기조정 중 (modal loop)
	bool			m_dpi_layout_pending = false;	//DPI 가 바뀌었는데 아직 레이아웃을 못 잡은 상태
	afx_msg void	OnEnterSizeMove();
	afx_msg void	OnExitSizeMove();
	afx_msg LRESULT on_message_HideOnStartup(WPARAM wParam, LPARAM lParam);

	//Vista+ clipboard listener: 클립보드가 변할 때마다 WM_CLIPBOARDUPDATE 가 도착.
	//SetClipboardViewer 체인 방식과 달리 체인 끊김 없음.
	afx_msg LRESULT OnClipboardUpdate(WPARAM wParam, LPARAM lParam);

	//글로벌 단축키 (RegisterHotKey 으로 등록). wParam = hotkeys 의 id.
	afx_msg LRESULT on_hotkey(WPARAM wParam, LPARAM lParam);

	//WM_DISPLAYCHANGE — 모니터 구성 변경 시 g_monitors 갱신 + 모니터 단축키 재등록.
	afx_msg LRESULT on_display_change(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
	afx_msg void OnBnClickedClipboardOnly();
};
