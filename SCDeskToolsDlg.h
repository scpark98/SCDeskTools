
// SCDeskToolsDlg.h: 헤더 파일
//

#pragma once

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
	CMenu			m_menu_main;	//다이얼로그 상단 메인 메뉴
	CSCColorPicker	m_color_picker;	//modeless 컬러 피커. OnInitDialog 에서 1회 create.
	Gdiplus::Color	m_cr_selected = Gdiplus::Color::Transparent;	//피커가 보낸 최근 색상.

	void			build_menus();
	void			show_tools_popup_menu(CPoint pt_screen);
	void			toggle_main_window();

	// 생성된 메시지 맵 함수
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg LRESULT on_message_CSysTrayIcon(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT on_message_CSCColorPicker(WPARAM wParam, LPARAM lParam);
	afx_msg void OnToolColorPicker();
	afx_msg void OnToolDropper();
	afx_msg void OnToolCaptureWindow();
	afx_msg void OnToolCaptureRegion();
	afx_msg void OnToolPasteClipboard();
	afx_msg void OnAppShowHide();
	afx_msg void OnAppAbout();
	afx_msg void OnAppExit();

	// 시작프로그램으로 실행 시 Shell_TrayWnd 가 뒤늦게 올라오는 경우 복구.
	// Explorer 크래시 후 재시작에도 동일하게 사용.
	static UINT s_msg_taskbar_created;
	afx_msg LRESULT OnTaskbarCreated(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
};
