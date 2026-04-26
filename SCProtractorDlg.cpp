// SCProtractorDlg.cpp

#include "pch.h"
#include "SCProtractorDlg.h"

#include <math.h>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")

IMPLEMENT_DYNAMIC(CSCProtractorDlg, CSCFrozenOverlayDlg)

BEGIN_MESSAGE_MAP(CSCProtractorDlg, CSCFrozenOverlayDlg)
END_MESSAGE_MAP()

namespace
{
	const int kHandleRadius     = 7;
	const int kHandleHitRadius  = 12;
	const int kArcRadius        = 60;
	const double kPi            = 3.14159265358979323846;

	double dist_sq(POINT a, POINT b)
	{
		double dx = double(a.x - b.x);
		double dy = double(a.y - b.y);
		return dx * dx + dy * dy;
	}
}

double CSCProtractorDlg::calc_angle_degrees() const
{
	double ax = double(m_arm_a.x - m_vertex.x);
	double ay = double(m_arm_a.y - m_vertex.y);
	double bx = double(m_arm_b.x - m_vertex.x);
	double by = double(m_arm_b.y - m_vertex.y);

	double la = sqrt(ax * ax + ay * ay);
	double lb = sqrt(bx * bx + by * by);
	if (la < 1e-6 || lb < 1e-6)
		return 0.0;

	double dot = (ax * bx + ay * by) / (la * lb);
	if (dot > 1.0)  dot = 1.0;
	if (dot < -1.0) dot = -1.0;
	return acos(dot) * 180.0 / kPi;
}

CSCProtractorDlg::HitTarget CSCProtractorDlg::hit_test(CPoint pt) const
{
	const double r2 = double(kHandleHitRadius) * kHandleHitRadius;
	if (dist_sq(pt, m_vertex) <= r2)
		return kHitVertex;
	if (dist_sq(pt, m_arm_a) <= r2)
		return kHitArmA;
	if (dist_sq(pt, m_arm_b) <= r2)
		return kHitArmB;
	return kHitNone;
}

void CSCProtractorDlg::move_vertex_to(CPoint new_vertex)
{
	CPoint delta = new_vertex - m_vertex;
	m_vertex = new_vertex;
	m_arm_a += delta;
	m_arm_b += delta;
}

void CSCProtractorDlg::on_mouse_down(UINT /*nFlags*/, CPoint point)
{
	switch (m_phase)
	{
	case Phase::kPlaceArmA:
		m_vertex = point;
		m_arm_a  = point;
		m_arm_b  = point;
		Invalidate(FALSE);
		return;

	case Phase::kPlaceArmB:
		m_arm_b = point;
		m_phase = Phase::kEdit;
		Invalidate(FALSE);
		return;

	case Phase::kEdit:
	{
		HitTarget t = hit_test(point);
		if (t != kHitNone)
		{
			m_drag_target = t;
			if (t == kHitVertex)
				m_drag_grab_offset = m_vertex - point;
		}
		return;
	}
	}
}

void CSCProtractorDlg::on_mouse_move(UINT nFlags, CPoint point)
{
	switch (m_phase)
	{
	case Phase::kPlaceArmA:
		if (nFlags & MK_LBUTTON)
		{
			m_arm_a = point;
			Invalidate(FALSE);
		}
		return;

	case Phase::kPlaceArmB:
		m_arm_b = point;
		Invalidate(FALSE);
		return;

	case Phase::kEdit:
		if (m_drag_target == kHitVertex)
		{
			move_vertex_to(point + m_drag_grab_offset);
			Invalidate(FALSE);
		}
		else if (m_drag_target == kHitArmA)
		{
			m_arm_a = point;
			Invalidate(FALSE);
		}
		else if (m_drag_target == kHitArmB)
		{
			m_arm_b = point;
			Invalidate(FALSE);
		}
		return;
	}
}

void CSCProtractorDlg::on_mouse_up(UINT /*nFlags*/, CPoint point)
{
	switch (m_phase)
	{
	case Phase::kPlaceArmA:
		if (dist_sq(point, m_vertex) < 4.0)
			return;
		m_arm_a = point;
		m_phase = Phase::kPlaceArmB;
		m_arm_b = m_vertex;
		Invalidate(FALSE);
		return;

	case Phase::kPlaceArmB:
		return;

	case Phase::kEdit:
		m_drag_target = kHitNone;
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
	if (m_phase != Phase::kEdit)
		return NULL;	//기본 십자 커서

	HitTarget t = hit_test(pt);
	if (t == kHitVertex)
		return ::LoadCursor(NULL, IDC_SIZEALL);
	if (t == kHitArmA || t == kHitArmB)
		return ::LoadCursor(NULL, IDC_HAND);
	return ::LoadCursor(NULL, IDC_ARROW);
}

void CSCProtractorDlg::on_overlay_paint(ID2D1DeviceContext* d2dc)
{
	if (m_phase == Phase::kPlaceArmA && m_vertex == m_arm_a)
	{
		//아직 첫 클릭 전 — 안내 문구만.
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

			const wchar_t* msg = L"각도기: 클릭+드래그로 첫 라인 → 마우스 이동 후 클릭으로 둘째 라인 → 핸들 드래그로 조정 / ESC=종료";
			d2dc->DrawText(msg, UINT32(wcslen(msg)), tf.Get(),
				D2D1::RectF(lx, ly, lx + w, ly + h),
				br_text.Get(),
				D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
		}
		return;
	}

	ComPtr<ID2D1SolidColorBrush> br_arm;
	ComPtr<ID2D1SolidColorBrush> br_handle_fill;
	ComPtr<ID2D1SolidColorBrush> br_handle_stroke;
	ComPtr<ID2D1SolidColorBrush> br_arc;
	ComPtr<ID2D1SolidColorBrush> br_arc_fill;
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0xFF3B30, 1.00f),            br_arm.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.00f), br_handle_fill.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0xFF3B30, 1.00f),            br_handle_stroke.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0xFFD60A, 1.00f),            br_arc.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0xFFD60A, 0.25f),            br_arc_fill.GetAddressOf());

	const D2D1_POINT_2F p_v = D2D1::Point2F(float(m_vertex.x), float(m_vertex.y));
	const D2D1_POINT_2F p_a = D2D1::Point2F(float(m_arm_a.x),  float(m_arm_a.y));
	const D2D1_POINT_2F p_b = D2D1::Point2F(float(m_arm_b.x),  float(m_arm_b.y));

	d2dc->DrawLine(p_v, p_a, br_arm.Get(), 2.0f);

	if (m_phase != Phase::kPlaceArmA)
		d2dc->DrawLine(p_v, p_b, br_arm.Get(), 2.0f);

	if (m_phase != Phase::kPlaceArmA)
	{
		double ax = double(m_arm_a.x - m_vertex.x);
		double ay = double(m_arm_a.y - m_vertex.y);
		double bx = double(m_arm_b.x - m_vertex.x);
		double by = double(m_arm_b.y - m_vertex.y);
		double la = sqrt(ax * ax + ay * ay);
		double lb = sqrt(bx * bx + by * by);

		if (la > 1e-3 && lb > 1e-3)
		{
			double angle_a = atan2(ay, ax);
			double angle_b = atan2(by, bx);

			double arc_radius = double(kArcRadius);
			double max_radius = (la < lb ? la : lb) * 0.6;
			if (arc_radius > max_radius)
				arc_radius = max_radius;
			if (arc_radius < 16.0)
				arc_radius = 16.0;

			double sweep = angle_b - angle_a;
			while (sweep <  0.0)         sweep += 2.0 * kPi;
			while (sweep >= 2.0 * kPi)   sweep -= 2.0 * kPi;
			bool clockwise = true;
			if (sweep > kPi)
			{
				sweep = 2.0 * kPi - sweep;
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
					seg.size  = D2D1::SizeF(float(arc_radius), float(arc_radius));
					seg.rotationAngle = 0.0f;
					seg.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
					seg.arcSize = D2D1_ARC_SIZE_SMALL;
					sink->AddArc(seg);
					sink->AddLine(p_v);
					sink->EndFigure(D2D1_FIGURE_END_CLOSED);
					sink->Close();

					d2dc->FillGeometry(arc_geom.Get(), br_arc_fill.Get());
					d2dc->DrawGeometry(arc_geom.Get(), br_arc.Get(), 1.5f);
				}
			}

			ComPtr<IDWriteFactory> dwrite;
			ComPtr<IDWriteTextFormat> tf;
			HRESULT hrf = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
				__uuidof(IDWriteFactory), (IUnknown**)dwrite.GetAddressOf());
			if (SUCCEEDED(hrf))
				dwrite->CreateTextFormat(L"Segoe UI", NULL,
					DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
					18.0f, L"", tf.GetAddressOf());
			if (tf)
			{
				tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

				double angle_deg = calc_angle_degrees();
				WCHAR text[64];
				swprintf_s(text, L"%.1f°", angle_deg);

				double mid = angle_start + sweep * 0.5;
				double label_dist = arc_radius + 28.0;
				float lx = float(p_v.x + cos(mid) * label_dist) - 50.0f;
				float ly = float(p_v.y + sin(mid) * label_dist) - 14.0f;

				const float lw = 100.0f;
				const float lh = 28.0f;
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
	}

	auto draw_handle = [&](D2D1_POINT_2F p)
	{
		D2D1_ELLIPSE e_outer = D2D1::Ellipse(p, float(kHandleRadius),     float(kHandleRadius));
		D2D1_ELLIPSE e_inner = D2D1::Ellipse(p, float(kHandleRadius - 2), float(kHandleRadius - 2));
		d2dc->FillEllipse(e_outer, br_handle_stroke.Get());
		d2dc->FillEllipse(e_inner, br_handle_fill.Get());
	};

	draw_handle(p_v);
	draw_handle(p_a);
	if (m_phase == Phase::kEdit)
		draw_handle(p_b);
}
