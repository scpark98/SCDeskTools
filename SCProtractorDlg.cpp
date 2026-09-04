// SCProtractorDlg.cpp

#include "pch.h"
#include "SCProtractorDlg.h"

#include <math.h>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")

IMPLEMENT_DYNAMIC(CSCProtractorDlg, CSCFrozenOverlayDlg)

BEGIN_MESSAGE_MAP(CSCProtractorDlg, CSCFrozenOverlayDlg)
END_MESSAGE_MAP()

//20260904 by claude. 96 DPI 기준 UI 크기. 실제 사용 시 scaled() 로 환산한다.
namespace
{
	const int handle_radius	= 7;
	const int handle_hit_radius	= 12;
	const int arc_radius_default	= 60;
	const double pi	= 3.14159265358979323846;

	double dist_sq(D2D1_POINT_2F a, D2D1_POINT_2F b)
	{
		double dx = double(a.x) - b.x;
		double dy = double(a.y) - b.y;
		return dx * dx + dy * dy;
	}

	D2D1_POINT_2F to_point(CPoint pt)
	{
		return D2D1::Point2F(float(pt.x), float(pt.y));
	}
}

double CSCProtractorDlg::calc_angle_degrees() const
{
	double ax = double(m_arm_a.x) - m_vertex.x;
	double ay = double(m_arm_a.y) - m_vertex.y;
	double bx = double(m_arm_b.x) - m_vertex.x;
	double by = double(m_arm_b.y) - m_vertex.y;

	double la = sqrt(ax * ax + ay * ay);
	double lb = sqrt(bx * bx + by * by);
	if (la < 1e-6 || lb < 1e-6)
		return 0.0;

	double dot = (ax * bx + ay * by) / (la * lb);
	if (dot > 1.0)	dot = 1.0;
	if (dot < -1.0) dot = -1.0;
	return acos(dot) * 180.0 / pi;
}

CSCProtractorDlg::HitTarget CSCProtractorDlg::hit_test(CPoint pt) const
{
	const double r2 = double(scaled(handle_hit_radius)) * scaled(handle_hit_radius);
	if (dist_sq(to_point(pt), m_vertex) <= r2)
		return ht_vertex;
	if (dist_sq(to_point(pt), m_arm_a) <= r2)
		return ht_arm_a;
	if (dist_sq(to_point(pt), m_arm_b) <= r2)
		return ht_arm_b;
	return ht_none;
}

void CSCProtractorDlg::move_vertex_to(D2D1_POINT_2F new_vertex)
{
	const float dx = new_vertex.x - m_vertex.x;
	const float dy = new_vertex.y - m_vertex.y;
	m_vertex = new_vertex;
	m_arm_a = D2D1::Point2F(m_arm_a.x + dx, m_arm_a.y + dy);
	m_arm_b = D2D1::Point2F(m_arm_b.x + dx, m_arm_b.y + dy);
}

void CSCProtractorDlg::on_mouse_down(UINT nFlags, CPoint point)
{
	switch (m_phase)
	{
	case Phase::phase_place_arm_a:
		m_vertex = to_point(point);
		m_arm_a	= m_vertex;
		m_arm_b	= m_vertex;
		Invalidate(FALSE);
		return;

	case Phase::phase_place_arm_b:
		//드래그 없이 클릭만으로 확정되는 단계라, on_mouse_move 에서 그려주던 것과 같은 스냅을 여기서도 적용해야
		//화면에 보이던 위치 그대로 확정된다.
		m_arm_b = snap_direction(m_vertex, to_point(point), snap_step_degrees(nFlags));
		m_phase = Phase::phase_edit;
		Invalidate(FALSE);
		return;

	case Phase::phase_edit:
	{
		HitTarget t = hit_test(point);
		if (t != ht_none)
		{
			m_drag_target = t;
			if (t == ht_vertex)
				m_drag_grab_offset = D2D1::Point2F(m_vertex.x - point.x, m_vertex.y - point.y);
		}
		return;
	}
	}
}

void CSCProtractorDlg::on_mouse_move(UINT nFlags, CPoint point)
{
	//각 arm 은 vertex 를 기준으로 스냅한다 — 30°/45° 같은 각을 정확히 만들 수 있다.
	//vertex 평행이동은 각도가 변하지 않으므로 스냅 대상이 아니다.
	const double step = snap_step_degrees(nFlags);

	switch (m_phase)
	{
	case Phase::phase_place_arm_a:
		if (nFlags & MK_LBUTTON)
		{
			m_arm_a = snap_direction(m_vertex, to_point(point), step);
			Invalidate(FALSE);
		}
		return;

	case Phase::phase_place_arm_b:
		m_arm_b = snap_direction(m_vertex, to_point(point), step);
		Invalidate(FALSE);
		return;

	case Phase::phase_edit:
		if (m_drag_target == ht_vertex)
		{
			move_vertex_to(D2D1::Point2F(point.x + m_drag_grab_offset.x, point.y + m_drag_grab_offset.y));
			Invalidate(FALSE);
		}
		else if (m_drag_target == ht_arm_a)
		{
			m_arm_a = snap_direction(m_vertex, to_point(point), step);
			Invalidate(FALSE);
		}
		else if (m_drag_target == ht_arm_b)
		{
			m_arm_b = snap_direction(m_vertex, to_point(point), step);
			Invalidate(FALSE);
		}
		return;
	}
}

void CSCProtractorDlg::on_mouse_up(UINT nFlags, CPoint point)
{
	switch (m_phase)
	{
	case Phase::phase_place_arm_a:
		if (dist_sq(to_point(point), m_vertex) < 4.0)
			return;
		m_arm_a = snap_direction(m_vertex, to_point(point), snap_step_degrees(nFlags));
		m_phase = Phase::phase_place_arm_b;
		m_arm_b = m_vertex;
		Invalidate(FALSE);
		return;

	case Phase::phase_place_arm_b:
		return;

	case Phase::phase_edit:
		m_drag_target = ht_none;
		return;
	}
}

bool CSCProtractorDlg::on_key_down(UINT nChar)
{
	if (nChar == VK_ESCAPE || nChar == VK_RETURN)
	{
		finish();
		return true;
	}
	return false;
}

HCURSOR CSCProtractorDlg::query_cursor(CPoint pt)
{
	if (m_phase != Phase::phase_edit)
		return NULL;	//기본 십자 커서

	HitTarget t = hit_test(pt);
	if (t == ht_vertex)
		return ::LoadCursor(NULL, IDC_SIZEALL);
	if (t == ht_arm_a || t == ht_arm_b)
		return ::LoadCursor(NULL, IDC_HAND);
	return ::LoadCursor(NULL, IDC_ARROW);
}

void CSCProtractorDlg::on_overlay_paint(ID2D1DeviceContext* d2dc)
{
	if (m_phase == Phase::phase_place_arm_a && dist_sq(m_vertex, m_arm_a) < 1e-6)
	{
		//아직 첫 클릭 전 — 안내 문구만.
		ComPtr<IDWriteFactory> dwrite;
		ComPtr<IDWriteTextFormat> tf;
		HRESULT hrf = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory), (IUnknown**)dwrite.GetAddressOf());
		if (SUCCEEDED(hrf))
			dwrite->CreateTextFormat(L"Segoe UI", NULL,
				DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
				scaled_f(16.0f), L"", tf.GetAddressOf());
		if (tf)
		{
			tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

			ComPtr<ID2D1SolidColorBrush> br_back;
			ComPtr<ID2D1SolidColorBrush> br_text;
			d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 0.65f), br_back.GetAddressOf());
			d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.00f), br_text.GetAddressOf());

			const float w = scaled_f(700.0f);
			const float h = scaled_f(32.0f);
			const float full_w = float(m_virtual_screen.Width());
			const float lx = (full_w - w) * 0.5f;
			const float ly = scaled_f(24.0f);
			D2D1_ROUNDED_RECT rr = { D2D1::RectF(lx, ly, lx + w, ly + h), scaled_f(6.0f), scaled_f(6.0f) };
			d2dc->FillRoundedRectangle(rr, br_back.Get());

			CStringW msg;
			msg.Format(L"각도기: 클릭+드래그로 첫 라인 → 마우스 이동 후 클릭으로 둘째 라인 → 핸들 드래그로 조정 / %s / ESC=종료", snap_hint_text());
			d2dc->DrawText(msg, UINT32(msg.GetLength()), tf.Get(),
				D2D1::RectF(lx, ly, lx + w, ly + h),
				br_text.Get(),
				D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
		}
		return;
	}

	ComPtr<ID2D1SolidColorBrush> br_arm;
	ComPtr<ID2D1SolidColorBrush> br_handle_stroke;
	ComPtr<ID2D1SolidColorBrush> br_arc;
	ComPtr<ID2D1SolidColorBrush> br_arc_fill;
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0xFF3B30, 1.00f),	br_arm.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0xFF3B30, 1.00f),	br_handle_stroke.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0xFFD60A, 1.00f),	br_arc.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0xFFD60A, 0.25f),	br_arc_fill.GetAddressOf());

	const D2D1_POINT_2F p_v = m_vertex;
	const D2D1_POINT_2F p_a = m_arm_a;
	const D2D1_POINT_2F p_b = m_arm_b;

	d2dc->DrawLine(p_v, p_a, br_arm.Get(), scaled_f(2.0f));

	if (m_phase != Phase::phase_place_arm_a)
		d2dc->DrawLine(p_v, p_b, br_arm.Get(), scaled_f(2.0f));

	if (m_phase != Phase::phase_place_arm_a)
	{
		double ax = double(m_arm_a.x) - m_vertex.x;
		double ay = double(m_arm_a.y) - m_vertex.y;
		double bx = double(m_arm_b.x) - m_vertex.x;
		double by = double(m_arm_b.y) - m_vertex.y;
		double la = sqrt(ax * ax + ay * ay);
		double lb = sqrt(bx * bx + by * by);

		if (la > 1e-3 && lb > 1e-3)
		{
			double angle_a = atan2(ay, ax);
			double angle_b = atan2(by, bx);

			double arc_radius = double(scaled(arc_radius_default));
			double max_radius = (la < lb ? la : lb) * 0.6;
			if (arc_radius > max_radius)
				arc_radius = max_radius;
			if (arc_radius < scaled(16))
				arc_radius = scaled(16);

			double sweep = angle_b - angle_a;
			while (sweep <	0.0)	sweep += 2.0 * pi;
			while (sweep >= 2.0 * pi)	sweep -= 2.0 * pi;
			bool clockwise = true;
			if (sweep > pi)
			{
				sweep = 2.0 * pi - sweep;
				clockwise = false;
			}

			const double angle_start = clockwise ? angle_a : angle_b;

			ComPtr<ID2D1Factory> factory;
			d2dc->GetFactory(factory.GetAddressOf());
			ComPtr<ID2D1PathGeometry> arc_geom;
			if (factory && SUCCEEDED(factory->CreatePathGeometry(arc_geom.GetAddressOf())))
			{
				ComPtr<ID2D1GeometrySink> sink;
				if (SUCCEEDED(arc_geom->Open(sink.GetAddressOf())))
				{
					D2D1_POINT_2F start = D2D1::Point2F(
						p_v.x + float(cos(angle_start) * arc_radius),
						p_v.y + float(sin(angle_start) * arc_radius));
					D2D1_POINT_2F end = D2D1::Point2F(
						p_v.x + float(cos(angle_start + sweep) * arc_radius),
						p_v.y + float(sin(angle_start + sweep) * arc_radius));

					sink->BeginFigure(p_v, D2D1_FIGURE_BEGIN_FILLED);
					sink->AddLine(start);
					D2D1_ARC_SEGMENT seg = {};
					seg.point = end;
					seg.size	= D2D1::SizeF(float(arc_radius), float(arc_radius));
					seg.rotationAngle = 0.0f;
					seg.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
					seg.arcSize = D2D1_ARC_SIZE_SMALL;
					sink->AddArc(seg);
					sink->AddLine(p_v);
					sink->EndFigure(D2D1_FIGURE_END_CLOSED);
					sink->Close();

					d2dc->FillGeometry(arc_geom.Get(), br_arc_fill.Get());
					d2dc->DrawGeometry(arc_geom.Get(), br_arc.Get(), scaled_f(1.5f));
				}
			}

			ComPtr<IDWriteFactory> dwrite;
			ComPtr<IDWriteTextFormat> tf;
			HRESULT hrf = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
				__uuidof(IDWriteFactory), (IUnknown**)dwrite.GetAddressOf());
			if (SUCCEEDED(hrf))
				dwrite->CreateTextFormat(L"Segoe UI", NULL,
					DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
					scaled_f(18.0f), L"", tf.GetAddressOf());
			if (tf)
			{
				tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

				double angle_deg = calc_angle_degrees();
				WCHAR text[64];
				swprintf_s(text, L"%.1f°", angle_deg);

				double mid = angle_start + sweep * 0.5;
				double label_dist = arc_radius + scaled(28);
				float lx = float(p_v.x + cos(mid) * label_dist) - scaled_f(50.0f);
				float ly = float(p_v.y + sin(mid) * label_dist) - scaled_f(14.0f);

				const float lw = scaled_f(100.0f);
				const float lh = scaled_f(28.0f);
				const float full_w = float(m_virtual_screen.Width());
				const float full_h = float(m_virtual_screen.Height());
				if (lx < 0)	lx = 0;
				if (ly < 0)	ly = 0;
				if (lx + lw > full_w)	lx = full_w - lw;
				if (ly + lh > full_h)	ly = full_h - lh;

				ComPtr<ID2D1SolidColorBrush> br_lbl_back;
				ComPtr<ID2D1SolidColorBrush> br_lbl_text;
				d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 0.78f), br_lbl_back.GetAddressOf());
				d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.00f), br_lbl_text.GetAddressOf());

				D2D1_ROUNDED_RECT rr = { D2D1::RectF(lx, ly, lx + lw, ly + lh), scaled_f(6.0f), scaled_f(6.0f) };
				d2dc->FillRoundedRectangle(rr, br_lbl_back.Get());
				d2dc->DrawText(text, UINT32(wcslen(text)), tf.Get(),
					D2D1::RectF(lx, ly, lx + lw, ly + lh),
					br_lbl_text.Get(),
					D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
			}
		}
	}

	//빈 원 (stroke 만). 흰 채움이 커서 끝점을 가려 정확한 위치 식별이 어려운 문제 해소
	//— 줄자와 동일 패턴.
	auto draw_handle = [&](D2D1_POINT_2F p)
	{
		const float r = scaled_f(float(handle_radius));
		D2D1_ELLIPSE e = D2D1::Ellipse(p, r, r);
		d2dc->DrawEllipse(e, br_handle_stroke.Get(), scaled_f(1.5f));
	};

	draw_handle(p_v);
	draw_handle(p_a);
	if (m_phase == Phase::phase_edit)
		draw_handle(p_b);
}
