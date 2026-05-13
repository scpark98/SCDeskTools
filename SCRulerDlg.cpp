// SCRulerDlg.cpp

#include "pch.h"
#include "SCRulerDlg.h"

#include <math.h>
#include <vector>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")

IMPLEMENT_DYNAMIC(CSCRulerDlg, CSCFrozenOverlayDlg)

BEGIN_MESSAGE_MAP(CSCRulerDlg, CSCFrozenOverlayDlg)
END_MESSAGE_MAP()

namespace
{
	const int handle_radius	= 7;
	const int handle_hit_radius	= 12;
	const int line_hit_dist	= 6;
	const double pi	= 3.14159265358979323846;

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
	const double r2 = double(handle_hit_radius) * handle_hit_radius;
	if (dist_sq(pt, m_start) <= r2)
		return ht_start;
	if (dist_sq(pt, m_end) <= r2)
		return ht_end;
	if (on_line(pt))
		return ht_line;
	return ht_none;
}

bool CSCRulerDlg::on_line(CPoint pt) const
{
	double t = 0.0;
	double d = perpendicular_dist(m_start, m_end, pt, t);
	return (t > 0.05 && t < 0.95 && d <= double(line_hit_dist));
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
	const double step = pi / 36.0;	//180° / 36 = 5°
	double snapped = floor((angle + step * 0.5) / step) * step;

	return CPoint(
		anchor.x + int(cos(snapped) * len + 0.5),
		anchor.y + int(sin(snapped) * len + 0.5));
}

void CSCRulerDlg::on_mouse_down(UINT /*nFlags*/, CPoint point)
{
	if (m_phase == Phase::phase_place_end && !m_placed)
	{
		//최초: start 잡고 drag 시작.
		m_start = point;
		m_end	= point;
		Invalidate(FALSE);
		return;
	}

	//편집 모드.
	HitTarget t = hit_test(point);
	if (t == ht_none)
	{
		//라인이 이미 그려진 후, 라인 외부 클릭 → 새로 그리기 시작.
		m_phase = Phase::phase_place_end;
		m_placed = false;
		m_start = point;
		m_end	= point;
		Invalidate(FALSE);
		return;
	}

	m_drag_target = t;
	if (t == ht_line)
	{
		//라인 평행이동: 라인 중심점을 잡은 것처럼 처리. start 와 마우스의 차이를 기억.
		m_drag_grab_offset = m_start - point;
	}
}

void CSCRulerDlg::on_mouse_move(UINT nFlags, CPoint point)
{
	const bool shift = (nFlags & MK_SHIFT) != 0;

	if (m_phase == Phase::phase_place_end && (nFlags & MK_LBUTTON))
	{
		m_end = shift ? apply_constrain(m_start, point) : point;
		Invalidate(FALSE);
		return;
	}

	if (m_phase == Phase::phase_edit)
	{
		if (m_drag_target == ht_start)
		{
			m_start = shift ? apply_constrain(m_end, point) : point;
			Invalidate(FALSE);
		}
		else if (m_drag_target == ht_end)
		{
			m_end = shift ? apply_constrain(m_start, point) : point;
			Invalidate(FALSE);
		}
		else if (m_drag_target == ht_line)
		{
			//라인 전체 평행이동. delta = 새 start - 현재 start.
			CPoint new_start = point + m_drag_grab_offset;
			CPoint delta = new_start - m_start;
			m_start += delta;
			m_end	+= delta;
			Invalidate(FALSE);
		}
	}
}

void CSCRulerDlg::on_mouse_up(UINT /*nFlags*/, CPoint point)
{
	if (m_phase == Phase::phase_place_end)
	{
		if (dist_sq(point, m_start) < 4.0)
			return;	//클릭만 한 셈 — 그대로 두고 다시 drag 대기
		m_phase = Phase::phase_edit;
		m_placed = true;
		Invalidate(FALSE);
		return;
	}

	m_drag_target = ht_none;
}

HCURSOR CSCRulerDlg::query_cursor(CPoint pt)
{
	if (m_phase != Phase::phase_edit)
		return NULL;

	HitTarget t = hit_test(pt);
	if (t == ht_start || t == ht_end)
		return ::LoadCursor(NULL, IDC_HAND);
	if (t == ht_line)
		return ::LoadCursor(NULL, IDC_SIZEALL);
	return NULL;
}

void CSCRulerDlg::on_overlay_paint(ID2D1DeviceContext* d2dc)
{
	if (m_phase == Phase::phase_place_end && !m_placed && m_start == m_end)
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
	const D2D1_POINT_2F p_e = D2D1::Point2F(float(m_end.x),	float(m_end.y));

	double dx = double(m_end.x - m_start.x);
	double dy = double(m_end.y - m_start.y);
	double length = sqrt(dx * dx + dy * dy);

	ComPtr<ID2D1SolidColorBrush> br_line;
	ComPtr<ID2D1SolidColorBrush> br_tick;
	ComPtr<ID2D1SolidColorBrush> br_handle_stroke;
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0x00B894, 1.00f), br_line.GetAddressOf());	//teal
	d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.00f), br_tick.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0x00B894, 1.00f), br_handle_stroke.GetAddressOf());

	d2dc->DrawLine(p_s, p_e, br_line.Get(), 2.0f);

	//tick mark — 라인을 따라 일정 간격마다 수직으로 짧은 선.
	//두 패스: (1) 검은 tick 을 CommandList 에 그려 Gaussian blur effect 적용 → 부드러운 그림자.
	//        (2) 그 위에 흰 tick 본체. 흰/검 양쪽 바탕에서 모두 가시.
	struct TickSeg { D2D1_POINT_2F base; D2D1_POINT_2F tip; };
	std::vector<TickSeg> ticks;
	if (length > 1.0)
	{
		double ux = dx / length;	//unit vector along line
		double uy = dy / length;
		double nx = -uy;			//perpendicular (one side)
		double ny =	ux;

		const int step_minor = 10;
		const int step_mid	= 50;
		const int step_major = 100;
		const int len_minor	= 4;
		const int len_mid	= 7;
		const int len_major	= 11;

		const int total = int(length);
		ticks.reserve(total / step_minor + 1);
		for (int i = step_minor; i < total; i += step_minor)
		{
			int tick_len;
			if (i % step_major == 0)	tick_len = len_major;
			else if (i % step_mid == 0)	tick_len = len_mid;
			else	tick_len = len_minor;

			D2D1_POINT_2F base = D2D1::Point2F(
				float(m_start.x + ux * i),
				float(m_start.y + uy * i));
			D2D1_POINT_2F tip = D2D1::Point2F(
				base.x + float(nx * tick_len),
				base.y + float(ny * tick_len));
			ticks.push_back({ base, tip });
		}
	}

	//Pass 1: tick 들 검정으로 CommandList 에 그리고 Gaussian blur 적용 → 메인 target 에 합성.
	if (!ticks.empty())
	{
		ComPtr<ID2D1CommandList> cmd_ticks;
		if (SUCCEEDED(d2dc->CreateCommandList(cmd_ticks.GetAddressOf())))
		{
			ComPtr<ID2D1Image> prev_target;
			d2dc->GetTarget(prev_target.GetAddressOf());
			d2dc->SetTarget(cmd_ticks.Get());

			ComPtr<ID2D1SolidColorBrush> br_black;
			d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black, 1.00f), br_black.GetAddressOf());
			for (const auto& t : ticks)
				d2dc->DrawLine(t.base, t.tip, br_black.Get(), 1.5f);

			d2dc->SetTarget(prev_target.Get());
			cmd_ticks->Close();

			ComPtr<ID2D1Effect> blur;
			if (SUCCEEDED(d2dc->CreateEffect(CLSID_D2D1GaussianBlur, blur.GetAddressOf())))
			{
				blur->SetInput(0, cmd_ticks.Get());
				//STANDARD_DEVIATION 픽셀 단위. 1.6 정도면 약 5px 폭의 부드러운 후광.
				blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 1.6f);
				d2dc->DrawImage(blur.Get());
			}
		}
	}

	//Pass 2: 흰 tick 본체.
	for (const auto& t : ticks)
		d2dc->DrawLine(t.base, t.tip, br_tick.Get(), 1.0f);

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
			double angle_deg = -atan2(dy, dx) * 180.0 / pi;

			//앱이 DPI Unaware 라 GetDeviceCaps(LOGPIXELSX) 가 OS 스케일과 무관하게 96 반환.
			//어떤 모니터 스케일에서도 동일 length → 동일 cm 로 일관. 명시 상수로 의도 표시.
			const double dpi = 96.0;
			const double length_cm = length / dpi * 2.54;

			WCHAR text[96];
			swprintf_s(text, L"%.1f px (%.2f cm)   %.1f°", length, length_cm, angle_deg);

			const float lw = 250.0f;
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
			if (lx < 0)	lx = 0;
			if (ly < 0)	ly = 0;
			if (lx + lw > full_w)	lx = full_w - lw;
			if (ly + lh > full_h)	ly = full_h - lh;

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

	//endpoint handles: 빈 원 (stroke 만). 흰 채움이 커서 끝점을 가려 정확한 측정 위치 식별
	//이 어려운 문제 해소 — 원 안쪽은 비워 라인 끝점과 픽셀이 그대로 보임.
	auto draw_handle = [&](D2D1_POINT_2F p)
	{
		D2D1_ELLIPSE e = D2D1::Ellipse(p, float(handle_radius), float(handle_radius));
		d2dc->DrawEllipse(e, br_handle_stroke.Get(), 1.5f);
	};
	draw_handle(p_s);
	draw_handle(p_e);
}
