// SCRulerDlg.h
// 화면 줄자. 두 점 사이의 길이(px) 와 각도를 측정.
//
// 사용 흐름 (Phase state machine):
//   1) PlaceEnd — LButtonDown 으로 start 설정 → drag → LButtonUp 으로 end 확정
//   2) Edit     — start / end / 라인 자체 (라인 위 = 평행이동) 모두 드래그 가능
// Shift = 0/45/90° 로 각도 제약 (방향 잠금).
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
	virtual void	on_mouse_up  (UINT nFlags, CPoint pt) override;
	virtual HCURSOR	query_cursor(CPoint pt) override;

private:
	enum class Phase
	{
		kPlaceEnd,	//start 누르고 drag 중
		kEdit,
	};

	enum HitTarget
	{
		kHitNone,
		kHitStart,
		kHitEnd,
		kHitLine,	//라인 위 (편집 모드에서 전체 평행이동)
	};

	Phase		m_phase = Phase::kPlaceEnd;
	bool		m_placed = false;	//PlaceEnd → Edit 로 한 번이라도 진입했는지

	CPoint		m_start = {};
	CPoint		m_end   = {};

	HitTarget	m_drag_target = kHitNone;
	CPoint		m_drag_grab_offset = {};	//평행이동 시 마우스와 잡은 점의 차이

	HitTarget	hit_test(CPoint pt) const;
	bool		on_line(CPoint pt) const;
	CPoint		apply_constrain(CPoint anchor, CPoint target) const;	//Shift 로 0/45/90° 스냅

	DECLARE_MESSAGE_MAP()
};
