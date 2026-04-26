// SCRegionCaptureDlg.h
// 영역 캡처 오버레이. 마우스 드래그로 사각형 영역 선택. 선택 외 영역은 어둡게 마스크.
// 좌클릭 드래그 시작 → 드래그 중 미리보기 → 좌버튼 떼면 확정. 우클릭 / ESC = 취소.

#pragma once

#include "SCFrozenOverlayDlg.h"

class CSCRegionCaptureDlg : public CSCFrozenOverlayDlg
{
	DECLARE_DYNAMIC(CSCRegionCaptureDlg)

public:
	CSCRegionCaptureDlg() = default;

	bool	is_picked()              const { return m_picked; }
	CRect	get_picked_rect_screen() const { return m_picked_rect_screen; }

protected:
	virtual void	on_overlay_paint(ID2D1DeviceContext* d2dc) override;
	virtual void	on_mouse_down(UINT nFlags, CPoint pt) override;
	virtual void	on_mouse_move(UINT nFlags, CPoint pt) override;
	virtual void	on_mouse_up  (UINT nFlags, CPoint pt) override;
	virtual bool	on_key_down(UINT nChar) override;	//방향키 = 1px 커서 이동 (정밀 위치 조정)
	virtual HCURSOR	query_cursor(CPoint pt) override;	//십자 커서 명시 (base 가 이걸 호출)
	afx_msg BOOL	OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);	//base virtual 체인 우회 — 직접 cross 설정

private:
	bool	m_dragging = false;
	CPoint	m_drag_anchor_client = {};
	CPoint	m_drag_cursor_client = {};

	bool	m_picked = false;
	CRect	m_picked_rect_screen = {};

	CRect	current_selection_client() const;

	DECLARE_MESSAGE_MAP()
};
