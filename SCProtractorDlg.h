// SCProtractorDlg.h
// 화면 각도기. 가상 데스크톱 1회 프리즈 캡처 위에 두 라인의 각도를 측정.
//
// 사용 흐름 (Phase state machine):
//   1) phase_place_arm_a — LButtonDown 으로 vertex 설정 → drag → LButtonUp 으로 arm A 확정
//   2) phase_place_arm_b — 마우스 이동만으로 arm B 끝점 따라옴 → LButtonDown 으로 arm B 확정
//   3) phase_edit        — vertex / arm A end / arm B end 3개 핸들 드래그로 회전 / 평행이동
// 각도 스냅: Shift=5° / Ctrl=15° / Shift+Ctrl=45° (베이스의 snap_step_degrees, 줄자와 동일).
// ESC / Enter / 우클릭 = 종료.

#pragma once

#include "SCFrozenOverlayDlg.h"

class CSCProtractorDlg : public CSCFrozenOverlayDlg
{
	DECLARE_DYNAMIC(CSCProtractorDlg)

public:
	CSCProtractorDlg() = default;

protected:
	virtual void	on_overlay_paint(ID2D1DeviceContext* d2dc) override;
	virtual void	on_mouse_down(UINT nFlags, CPoint pt) override;
	virtual void	on_mouse_move(UINT nFlags, CPoint pt) override;
	virtual void	on_mouse_up	(UINT nFlags, CPoint pt) override;
	virtual bool	on_key_down(UINT nChar) override;
	virtual HCURSOR	query_cursor(CPoint pt) override;

private:
	enum class Phase
	{
		phase_place_arm_a,
		phase_place_arm_b,
		phase_edit,
	};

	enum HitTarget
	{
		ht_none,
		ht_vertex,
		ht_arm_a,
		ht_arm_b,
	};

	Phase		m_phase = Phase::phase_place_arm_a;

	//정수 픽셀로 보관하면 스냅한 각도가 반올림 때문에 다시 어긋나 각도 표시가 미세하게 떨린다 (줄자와 동일).
	D2D1_POINT_2F	m_vertex = {};
	D2D1_POINT_2F	m_arm_a	= {};
	D2D1_POINT_2F	m_arm_b	= {};

	HitTarget	m_drag_target = ht_none;
	D2D1_POINT_2F	m_drag_grab_offset = {};

	double		calc_angle_degrees() const;
	HitTarget	hit_test(CPoint pt) const;
	void		move_vertex_to(D2D1_POINT_2F new_vertex);

	DECLARE_MESSAGE_MAP()
};
