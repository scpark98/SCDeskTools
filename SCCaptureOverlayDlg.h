// SCCaptureOverlayDlg.h
// 화면 전체를 가상 데스크톱 단위로 1회 캡처하여 풀스크린 topmost 오버레이로 표시.
// 마우스 이동 시 커서 아래 top-level window 의 rect 를 검출하여 보더로 강조.
// 좌클릭 = 해당 윈도우 선택, 우클릭/ESC = 취소.
//
// 호출 패턴은 SCDropperDlg 와 동일한 self-message-loop (DoModal 아님).
//
// [향후 Common 승격 조건]
//   - SCDeskTools 외부에서 재사용이 발생하는 시점.
//   - rect 선택 외에 영역 드래그 모드(Region) 까지 같이 통합되는 시점.

#pragma once

#include <afxwin.h>
#include "Common/directx/CSCD2Context/SCD2Context.h"
#include "Common/directx/CSCD2Image/SCD2Image.h"

class CSCCaptureOverlayDlg : public CDialog
{
	DECLARE_DYNAMIC(CSCCaptureOverlayDlg)

public:
	CSCCaptureOverlayDlg();
	virtual ~CSCCaptureOverlayDlg();

	//parent 는 모달처럼 보이게 하고 싶은 메인 창. NULL 도 허용.
	bool		create(CWnd* parent);

	bool		is_picked()                const { return m_picked; }
	CRect		get_picked_rect_screen()   const { return m_picked_rect_screen; }
	HWND		get_picked_hwnd()          const { return m_picked_hwnd; }
	CRect		get_virtual_screen_rect()  const { return m_virtual_screen; }
	CSCD2Image*	get_frozen_image()               { return &m_frozen; }
	CSCD2Context* get_d2_context()                { return &m_d2; }
	HBITMAP		get_frozen_hbitmap()       const { return m_frozen_hbmp; }	//전체 가상 스크린 DIB. 좌표 = virtual screen 기준

private:
	CSCD2Context	m_d2;
	CSCD2Image		m_frozen;
	HBITMAP			m_frozen_hbmp = NULL;	//프리즈 캡처 DIB section. ~Dlg 에서 정리.

	CRect			m_virtual_screen = {};	//가상 데스크톱 (모든 모니터) screen coord
	HWND			m_target_hwnd = NULL;
	CRect			m_target_rect_screen = {};
	bool			m_picked = false;
	CRect			m_picked_rect_screen = {};
	HWND			m_picked_hwnd = NULL;

	bool			capture_virtual_screen_to_d2();
	void			update_target_under_cursor();
	void			finish(bool picked);

	afx_msg void	OnPaint();
	afx_msg BOOL	OnEraseBkgnd(CDC* pDC);
	afx_msg void	OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void	OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void	OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void	OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	virtual BOOL	PreTranslateMessage(MSG* pMsg);

	DECLARE_MESSAGE_MAP()
};
