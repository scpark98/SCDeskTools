// SCFreehandCaptureDlg.h
// 자유선 (freehand) 캡처 오버레이.
//
// 흐름 (1-phase):
//   - 좌클릭 드래그로 경로 그림. 마우스를 떼면 즉시 폴리곤으로 닫고 캡처 확정.
//   - 경로 점이 3 개 미만이면 취소.
//   - ESC / 우클릭 = 취소.
//
// 결과: get_picked_path_screen() = 화면 좌표 폴리곤 점 배열,
//       get_picked_bounds_screen() = 폴리곤 bounding box (화면 좌표).

#pragma once

#include "SCFrozenOverlayDlg.h"
#include <vector>

class CSCFreehandCaptureDlg : public CSCFrozenOverlayDlg
{
	DECLARE_DYNAMIC(CSCFreehandCaptureDlg)

public:
	CSCFreehandCaptureDlg() = default;

	bool							is_picked() const { return m_picked; }
	const std::vector<CPoint>&		get_picked_path_screen() const { return m_picked_path_screen; }
	CRect							get_picked_bounds_screen() const { return m_picked_bounds_screen; }

protected:
	virtual void	on_overlay_paint(ID2D1DeviceContext* d2dc) override;
	virtual void	on_mouse_down(UINT nFlags, CPoint pt) override;
	virtual void	on_mouse_move(UINT nFlags, CPoint pt) override;
	virtual void	on_mouse_up	(UINT nFlags, CPoint pt) override;
	virtual HCURSOR	query_cursor(CPoint pt) override;

private:
	std::vector<CPoint>		m_path;					//현재 그리고 있는 경로 (client 좌표)
	bool					m_drawing = false;

	bool					m_picked = false;
	std::vector<CPoint>		m_picked_path_screen;
	CRect					m_picked_bounds_screen = {};

	DECLARE_MESSAGE_MAP()
};
