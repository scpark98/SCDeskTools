
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
	Gdiplus::Color	m_cr_selected = Gdiplus::Color::Transparent;	//피커가 보낸 최근 색상.

	//메인 창에 노출할 즐겨찾기 툴 ID 목록. 향후 설정창에서 편집 → 레지스트리 저장 예정.
	//지금은 OnInitDialog 에서 kDefaultFavorites 로 초기화.
	std::vector<UINT>							m_favorites;
	std::vector<std::unique_ptr<CButton>>		m_btns_favorite;	//m_favorites 와 1:1 매핑, 동적 생성.

	//개발 편의용 영구 종료 버튼. 트레이에서 종료하는 절차 생략 위해 우측 하단에 항상 노출.
	//배포 시 build_dev_exit_button 호출만 빼면 깔끔히 제거 가능.
	CButton			m_btn_dev_exit;

	void			build_buttons();
	void			build_dev_exit_button();
	void			send_image_to_clipboard_and_note(const BYTE* bgra_top_down, int w, int h, POINT note_pos);
	void			capture_screen_rect(const CRect& rc_screen);
	void			enum_monitor_rects(std::vector<CRect>& out) const;
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
	static UINT s_msg_taskbar_created;
	afx_msg LRESULT OnTaskbarCreated(WPARAM wParam, LPARAM lParam);

	//Vista+ clipboard listener: 클립보드가 변할 때마다 WM_CLIPBOARDUPDATE 가 도착.
	//SetClipboardViewer 체인 방식과 달리 체인 끊김 없음.
	afx_msg LRESULT OnClipboardUpdate(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
};
