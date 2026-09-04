// SCRulerDlg.h
// 화면 줄자. 두 점 사이의 길이(px) 와 각도를 측정.
//
// 사용 흐름 (Phase state machine):
//   1) phase_place_end — LButtonDown 으로 start 설정 → drag → LButtonUp 으로 end 확정
//   2) phase_edit      — start / end / 라인 자체 (라인 위 = 평행이동) 모두 드래그 가능
// 각도 스냅: Shift=5° / Ctrl=15° / Shift+Ctrl=45° (베이스의 snap_step_degrees).
// ESC / 우클릭 = 종료.

#pragma once

#include "SCFrozenOverlayDlg.h"

class CSCRulerDlg : public CSCFrozenOverlayDlg
{
	DECLARE_DYNAMIC(CSCRulerDlg)

public:
	CSCRulerDlg() = default;

protected:
	virtual void	on_overlay_paint(ID2D1DeviceContext* d2dc) override;
	virtual void	on_mouse_down(UINT nFlags, CPoint pt) override;
	virtual void	on_mouse_move(UINT nFlags, CPoint pt) override;
	virtual void	on_mouse_up	(UINT nFlags, CPoint pt) override;
	virtual HCURSOR	query_cursor(CPoint pt) override;

private:
	enum class Phase
	{
		phase_place_end,	//start 누르고 drag 중
		phase_edit,
	};

	enum HitTarget
	{
		ht_none,
		ht_start,
		ht_end,
		ht_line,	//라인 위 (편집 모드에서 전체 평행이동)
	};

	Phase		m_phase = Phase::phase_place_end;
	bool		m_placed = false;	//phase_place_end → phase_edit 로 한 번이라도 진입했는지

	//정수 픽셀로 보관하면 스냅한 각도가 반올림 때문에 다시 어긋나 라인과 각도 표시가 미세하게 떨린다.
	D2D1_POINT_2F	m_start = {};
	D2D1_POINT_2F	m_end	= {};

	HitTarget	m_drag_target = ht_none;
	D2D1_POINT_2F	m_drag_grab_offset = {};	//평행이동 시 마우스와 잡은 점의 차이

	HitTarget	hit_test(CPoint pt) const;
	bool		on_line(CPoint pt) const;

	DECLARE_MESSAGE_MAP()
};
