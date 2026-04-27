// SCRegionCaptureDlg.h
// 영역 캡처 오버레이.
//
// 흐름 (2-phase):
//   1) kDragging — 좌클릭 드래그로 영역 정의. 떼는 순간:
//      - Shift 미보유 → 즉시 캡처.
//      - Shift 보유   → kEdit 진입 (정밀 보정).
//   2) kEdit     — 8 핸들 (코너 4 + 변 4) 로 resize, ROI 내부 드래그로 이동, 방향키 nudge.
//                  Enter / 더블클릭 = 캡처 확정. ESC / 우클릭 = 취소.
//
// 드래그 중과 Edit 중 모두 사이즈 라벨 + modifier hint 노출.

#pragma once

#include "SCFrozenOverlayDlg.h"

class CSCRegionCaptureDlg : public CSCFrozenOverlayDlg
{
	DECLARE_DYNAMIC(CSCRegionCaptureDlg)

public:
	CSCRegionCaptureDlg() = default;

	bool	is_picked()					const { return m_picked; }
	CRect	get_picked_rect_screen()	const { return m_picked_rect_screen; }

protected:
	virtual void	on_overlay_paint(ID2D1DeviceContext* d2dc) override;
	virtual void	on_mouse_down(UINT nFlags, CPoint pt) override;
	virtual void	on_mouse_move(UINT nFlags, CPoint pt) override;
	virtual void	on_mouse_up	(UINT nFlags, CPoint pt) override;
	virtual bool	on_key_down(UINT nChar) override;
	virtual HCURSOR	query_cursor(CPoint pt) override;
	afx_msg BOOL	OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void	OnLButtonDblClk(UINT nFlags, CPoint point);

private:
	enum class Phase
	{
		kDragging,
		kEdit,
	};

	enum HitTarget
	{
		kHitNone,
		kHitTopLeft,	kHitTop,	kHitTopRight,
		kHitLeft,		kHitInterior,	kHitRight,
		kHitBottomLeft,	kHitBottom,	kHitBottomRight,
	};

	Phase		m_phase = Phase::kDragging;

	bool		m_dragging = false;
	CPoint		m_drag_anchor_client = {};
	CPoint		m_drag_cursor_client = {};

	CRect		m_edit_rect = {};
	HitTarget	m_edit_grab = kHitNone;
	CPoint		m_edit_grab_offset = {};

	bool		m_picked = false;
	CRect		m_picked_rect_screen = {};

	CRect			current_selection_client() const;
	HitTarget		hit_test(CPoint pt) const;
	static LPCTSTR	cursor_id_for_hit(HitTarget h);
	void			commit_capture();

	DECLARE_MESSAGE_MAP()
};
