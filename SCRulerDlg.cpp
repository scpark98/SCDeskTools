// SCRulerDlg.cpp

#include "pch.h"
#include "SCRulerDlg.h"

#include <math.h>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")

IMPLEMENT_DYNAMIC(CSCRulerDlg, CSCFrozenOverlayDlg)

BEGIN_MESSAGE_MAP(CSCRulerDlg, CSCFrozenOverlayDlg)
END_MESSAGE_MAP()

namespace
{
	const int kHandleRadius     = 7;
	const int kHandleHitRadius  = 12;
	const int kLineHitDist      = 6;
	const double kPi            = 3.14159265358979323846;

	double dist_sq(POINT a, POINT b)
	{
		double dx = double(a.x - b.x);
		double dy = double(a.y - b.y);
		return dx * dx + dy * dy;
	}

	//라인 (a→b) 에 대한 점 p 의 수직 거리. p 가 [a,b] 사이에 투영될 때만 의미 있음.
	//projection_t out 으로 0~1 범위 투영 위치도 같이 반환.
	double perpendicular_dist(POINT a, POINT b, POINT p, double& projection_t)
	{
		double dx = double(b.x - a.x);
		double dy = double(b.y - a.y);
		double len2 = dx * dx + dy * dy;
		if (len2 < 1e-6)
		{
			projection_t = 0.0;
			return 1e9;
		}
		projection_t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
		double cross = (p.x - a.x) * dy - (p.y - a.y) * dx;
		return fabs(cross) / sqrt(len2);
	}
}

CSCRulerDlg::HitTarget CSCRulerDlg::hit_test(CPoint pt) const
{
	const double r2 = double(kHandleHitRadius) * kHandleHitRadius;
	if (dist_sq(pt, m_start) <= r2)
		return kHitStart;
	if (dist_sq(pt, m_end) <= r2)
		return kHitEnd;
	if (on_line(pt))
		return kHitLine;
	return kHitNone;
}

bool CSCRulerDlg::on_line(CPoint pt) const
{
	double t = 0.0;
	double d = perpendicular_dist(m_start, m_end, pt, t);
	return (t > 0.05 && t < 0.95 && d <= double(kLineHitDist));
}

CPoint CSCRulerDlg::apply_constrain(CPoint anchor, CPoint target) const
{
	double dx = double(target.x - anchor.x);
	double dy = double(target.y - anchor.y);
	double len = sqrt(dx * dx + dy * dy);
	if (len < 1e-3)
		return target;

	double angle = atan2(dy, dx);
	//5° 단위 스냅. 디자인 도구의 45° 보다 fine — 기울기 측정 시 의도한 각도를 잡기 쉽다.
	const double step = kPi / 36.0;	//180° / 36 = 5°
	double snapped = floor((angle + step * 0.5) / step) * step;

	return CPoint(
		anchor.x + int(cos(snapped) * len + 0.5),
		anchor.y + int(sin(snapped) * len + 0.5));
}

void CSCRulerDlg::on_mouse_down(UINT /*nFlags*/, CPoint point)
{
	if (m_phase == Phase::kPlaceEnd && !m_placed)
	{
		//최초: start 잡고 drag 시작.
		m_start = point;
		m_end   = point;
		Invalidate(FALSE);
		return;
	}

	//편집 모드.
	HitTarget t = hit_test(point);
	if (t == kHitNone)
	{
		//라인이 이미 그려진 후, 라인 외부 클릭 → 새로 그리기 시작.
		m_phase = Phase::kPlaceEnd;
		m_placed = false;
		m_start = point;
		m_end   = point;
		Invalidate(FALSE);
		return;
	}

	m_drag_target = t;
	if (t == kHitLine)
	{
		//라인 평행이동: 라인 중심점을 잡은 것처럼 처리. start 와 마우스의 차이를 기억.
		m_drag_grab_offset = m_start - point;
	}
}

void CSCRulerDlg::on_mouse_move(UINT nFlags, CPoint point)
{
	const bool shift = (nFlags & MK_SHIFT) != 0;

	if (m_phase == Phase::kPlaceEnd && (nFlags & MK_LBUTTON))
	{
		m_end = shift ? apply_constrain(m_start, point) : point;
		Invalidate(FALSE);
		return;
	}

	if (m_phase == Phase::kEdit)
	{
		if (m_drag_target == kHitStart)
		{
			m_start = shift ? apply_constrain(m_end, point) : point;
			Invalidate(FALSE);
		}
		else if (m_drag_target == kHitEnd)
		{
			m_end = shift ? apply_constrain(m_start, point) : point;
			Invalidate(FALSE);
		}
		else if (m_drag_target == kHitLine)
		{
			//라인 전체 평행이동. delta = 새 start - 현재 start.
			CPoint new_start = point + m_drag_grab_offset;
			CPoint delta = new_start - m_start;
			m_start += delta;
			m_end   += delta;
			Invalidate(FALSE);
		}
	}
}

void CSCRulerDlg::on_mouse_up(UINT /*nFlags*/, CPoint point)
{
	if (m_phase == Phase::kPlaceEnd)
	{
		if (dist_sq(point, m_start) < 4.0)
			return;	//클릭만 한 셈 — 그대로 두고 다시 drag 대기
		m_phase = Phase::kEdit;
		m_placed = true;
		Invalidate(FALSE);
		return;
	}

	m_drag_target = kHitNone;
}

HCURSOR CSCRulerDlg::query_cursor(CPoint pt)
{
	if (m_phase != Phase::kEdit)
		return NULL;

	HitTarget t = hit_test(pt);
	if (t == kHitStart || t == kHitEnd)
		return ::LoadCursor(NULL, IDC_HAND);
	if (t == kHitLine)
		return ::LoadCursor(NULL, IDC_SIZEALL);
	return NULL;
}

void CSCRulerDlg::on_overlay_paint(ID2D1DeviceContext* d2dc)
{
	if (m_phase == Phase::kPlaceEnd && !m_placed && m_start == m_end)
	{
		//아직 첫 클릭 전 — 안내 문구.
		ComPtr<IDWriteFactory> dwrite;
		ComPtr<IDWriteTextFormat> tf;
		HRESULT hrf = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory), (IUnknown**)dwrite.GetAddressOf());
		if (SUCCEEDED(hrf))
			dwrite->CreateTextFormat(L"Segoe UI", NULL,
				DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				16.0f, L"", tf.GetAddressOf());
		if (tf)
		{
			tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

			ComPtr<ID2D1SolidColorBrush> br_back;
			ComPtr<ID2D1SolidColorBrush> br_text;
			d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 0.65f), br_back.GetAddressOf());
			d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.00f), br_text.GetAddressOf());

			const float w = 540.0f;
			const float h = 32.0f;
			const float full_w = float(m_virtual_screen.Width());
			const float lx = (full_w - w) * 0.5f;
			const float ly = 24.0f;
			D2D1_ROUNDED_RECT rr = { D2D1::RectF(lx, ly, lx + w, ly + h), 6.0f, 6.0f };
			d2dc->FillRoundedRectangle(rr, br_back.Get());

			const wchar_t* msg = L"줄자: 클릭+드래그로 라인 그리기 → 끝점/라인 드래그로 조정 / Shift=5° 단위 스냅 / ESC=종료";
			d2dc->DrawText(msg, UINT32(wcslen(msg)), tf.Get(),
				D2D1::RectF(lx, ly, lx + w, ly + h),
				br_text.Get(),
				D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
		}
		return;
	}

	const D2D1_POINT_2F p_s = D2D1::Point2F(float(m_start.x), float(m_start.y));
	const D2D1_POINT_2F p_e = D2D1::Point2F(float(m_end.x),   float(m_end.y));

	double dx = double(m_end.x - m_start.x);
	double dy = double(m_end.y - m_start.y);
	double length = sqrt(dx * dx + dy * dy);

	ComPtr<ID2D1SolidColorBrush> br_line;
	ComPtr<ID2D1SolidColorBrush> br_tick;
	ComPtr<ID2D1SolidColorBrush> br_handle_fill;
	ComPtr<ID2D1SolidColorBrush> br_handle_stroke;
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0x00B894, 1.00f), br_line.GetAddressOf());          //teal
	d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 0.85f), br_tick.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.00f), br_handle_fill.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0x00B894, 1.00f), br_handle_stroke.GetAddressOf());

	d2dc->DrawLine(p_s, p_e, br_line.Get(), 2.0f);

	//tick mark — 라인을 따라 일정 간격마다 수직으로 짧은 선.
	if (length > 1.0)
	{
		double ux = dx / length;	//unit vector along line
		double uy = dy / length;
		double nx = -uy;			//perpendicular (one side)
		double ny =  ux;

		const int step_minor = 10;
		const int step_mid   = 50;
		const int step_major = 100;
		const int len_minor  = 4;
		const int len_mid    = 7;
		const int len_major  = 11;

		const int total = int(length);
		for (int i = step_minor; i < total; i += step_minor)
		{
			int tick_len;
			if (i % step_major == 0)      tick_len = len_major;
			else if (i % step_mid == 0)   tick_len = len_mid;
			else                          tick_len = len_minor;

			D2D1_POINT_2F base = D2D1::Point2F(
				float(m_start.x + ux * i),
				float(m_start.y + uy * i));
			D2D1_POINT_2F tip = D2D1::Point2F(
				base.x + float(nx * tick_len),
				base.y + float(ny * tick_len));
			d2dc->DrawLine(base, tip, br_tick.Get(), 1.0f);
		}
	}

	//길이 + 각도 라벨. 라인 중점에서 수직 방향으로 약간 떨어뜨림.
	{
		ComPtr<IDWriteFactory> dwrite;
		ComPtr<IDWriteTextFormat> tf;
		HRESULT hrf = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory), (IUnknown**)dwrite.GetAddressOf());
		if (SUCCEEDED(hrf))
			dwrite->CreateTextFormat(L"Segoe UI", NULL,
				DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				16.0f, L"", tf.GetAddressOf());
		if (tf)
		{
			tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

			//각도는 화면 좌표계 (y 아래) 기준 atan2 → 일반 수학 좌표 (y 위) 로 부호 뒤집어 표시.
			double angle_deg = -atan2(dy, dx) * 180.0 / kPi;

			WCHAR text[64];
			swprintf_s(text, L"%.1f px   %.1f°", length, angle_deg);

			const float lw = 180.0f;
			const float lh = 28.0f;

			//중점 기준, 라인의 수직 방향(왼쪽) 으로 lh + 라벨 절반 만큼 옮김.
			float cx = (p_s.x + p_e.x) * 0.5f;
			float cy = (p_s.y + p_e.y) * 0.5f;

			float nx = 0.0f, ny = -1.0f;
			if (length > 1.0)
			{
				double ux = dx / length;
				double uy = dy / length;
				nx = float(-uy);
				ny = float( ux);
				//라벨이 라인 위쪽으로 가도록 (ny < 0).
				if (ny > 0)
				{
					nx = -nx;
					ny = -ny;
				}
			}

			float lx = cx + nx * 22.0f - lw * 0.5f;
			float ly = cy + ny * 22.0f - lh * 0.5f;

			//화면 가장자리 클램프.
			const float full_w = float(m_virtual_screen.Width());
			const float full_h = float(m_virtual_screen.Height());
			if (lx < 0)             lx = 0;
			if (ly < 0)             ly = 0;
			if (lx + lw > full_w)   lx = full_w - lw;
			if (ly + lh > full_h)   ly = full_h - lh;

			ComPtr<ID2D1SolidColorBrush> br_lbl_back;
			ComPtr<ID2D1SolidColorBrush> br_lbl_text;
			d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 0.78f), br_lbl_back.GetAddressOf());
			d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.00f), br_lbl_text.GetAddressOf());

			D2D1_ROUNDED_RECT rr = { D2D1::RectF(lx, ly, lx + lw, ly + lh), 6.0f, 6.0f };
			d2dc->FillRoundedRectangle(rr, br_lbl_back.Get());
			d2dc->DrawText(text, UINT32(wcslen(text)), tf.Get(),
				D2D1::RectF(lx, ly, lx + lw, ly + lh),
				br_lbl_text.Get(),
				D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
		}
	}

	//endpoint handles.
	auto draw_handle = [&](D2D1_POINT_2F p)
	{
		D2D1_ELLIPSE e_outer = D2D1::Ellipse(p, float(kHandleRadius),     float(kHandleRadius));
		D2D1_ELLIPSE e_inner = D2D1::Ellipse(p, float(kHandleRadius - 2), float(kHandleRadius - 2));
		d2dc->FillEllipse(e_outer, br_handle_stroke.Get());
		d2dc->FillEllipse(e_inner, br_handle_fill.Get());
	};
	draw_handle(p_s);
	draw_handle(p_e);
}
