// SCProtractorDlg.h
// 화면 각도기. 가상 데스크톱 1회 프리즈 캡처 위에 두 라인의 각도를 측정.
//
// 사용 흐름 (Phase state machine):
//   1) PlaceArmA — LButtonDown 으로 vertex 설정 → drag → LButtonUp 으로 arm A 확정
//   2) PlaceArmB — 마우스 이동만으로 arm B 끝점 따라옴 → LButtonDown 으로 arm B 확정
//   3) Edit      — vertex / arm A end / arm B end 3개 핸들 드래그로 회전 / 평행이동
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
		kPlaceArmA,
		kPlaceArmB,
		kEdit,
	};

	enum HitTarget
	{
		kHitNone,
		kHitVertex,
		kHitArmA,
		kHitArmB,
	};

	Phase		m_phase = Phase::kPlaceArmA;

	CPoint		m_vertex = {};
	CPoint		m_arm_a	= {};
	CPoint		m_arm_b	= {};

	HitTarget	m_drag_target = kHitNone;
	CPoint		m_drag_grab_offset = {};

	double		calc_angle_degrees() const;
	HitTarget	hit_test(CPoint pt) const;
	void		move_vertex_to(CPoint new_vertex);

	DECLARE_MESSAGE_MAP()
};
