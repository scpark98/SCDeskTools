// SCCaptureOverlayDlg.h
// 창 캡처 오버레이. 마우스 아래 top-level window 의 rect 를 검출하여 강조.
// 좌클릭 = 해당 윈도우 선택, 우클릭 / ESC = 취소.
//
// 베이스 CSCFrozenOverlayDlg 가 프리즈 캡처 / 메시지 라우팅 / self-message-loop / finish 처리.

#pragma once

#include "SCFrozenOverlayDlg.h"

class CSCCaptureOverlayDlg : public CSCFrozenOverlayDlg
{
	DECLARE_DYNAMIC(CSCCaptureOverlayDlg)

public:
	CSCCaptureOverlayDlg() = default;

	bool	is_picked()	const { return m_picked; }
	CRect	get_picked_rect_screen()	const { return m_picked_rect_screen; }
	HWND	get_picked_hwnd()	const { return m_picked_hwnd; }

protected:
	virtual void	on_overlay_paint(ID2D1DeviceContext* d2dc) override;
	virtual void	on_mouse_move (UINT nFlags, CPoint pt) override;
	virtual void	on_mouse_down (UINT nFlags, CPoint pt) override;

private:
	HWND	m_target_hwnd = NULL;
	CRect	m_target_rect_screen = {};

	bool	m_picked = false;
	CRect	m_picked_rect_screen = {};
	HWND	m_picked_hwnd = NULL;

	void	update_target_under_cursor();

	DECLARE_MESSAGE_MAP()
};
