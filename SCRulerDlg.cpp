// SCRulerDlg.cpp

#include "pch.h"
#include "SCRulerDlg.h"

#include <math.h>
#include <vector>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")

#include "Common/win_compat/dpi.h"

IMPLEMENT_DYNAMIC(CSCRulerDlg, CSCFrozenOverlayDlg)

BEGIN_MESSAGE_MAP(CSCRulerDlg, CSCFrozenOverlayDlg)
END_MESSAGE_MAP()

//20260904 by claude. 96 DPI 기준 UI 크기. 실제 사용 시 scaled() 로 환산한다.
//tick 간격(10/50/100)은 여기 없다 — 그건 UI 가 아니라 "몇 픽셀마다 눈금" 이라는 측정 단위라
//물리 픽셀 그대로여야 한다. 눈금 길이만 UI 다.
namespace
{
	const int handle_radius	= 7;
	const int handle_hit_radius	= 12;
	const int line_hit_dist	= 6;
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

	//라인 (a→b) 에 대한 점 p 의 수직 거리. p 가 [a,b] 사이에 투영될 때만 의미 있음.
	//projection_t out 으로 0~1 범위 투영 위치도 같이 반환.
	double perpendicular_dist(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_POINT_2F p, double& projection_t)
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
	const double r2 = double(scaled(handle_hit_radius)) * scaled(handle_hit_radius);
	if (dist_sq(to_point(pt), m_start) <= r2)
		return ht_start;
	if (dist_sq(to_point(pt), m_end) <= r2)
		return ht_end;
	if (on_line(pt))
		return ht_line;
	return ht_none;
}

bool CSCRulerDlg::on_line(CPoint pt) const
{
	double t = 0.0;
	double d = perpendicular_dist(m_start, m_end, to_point(pt), t);
	return (t > 0.05 && t < 0.95 && d <= double(scaled(line_hit_dist)));
}

void CSCRulerDlg::on_mouse_down(UINT /*nFlags*/, CPoint point)
{
	if (m_phase == Phase::phase_place_end && !m_placed)
	{
		//최초: start 잡고 drag 시작.
		m_start = to_point(point);
		m_end	= m_start;
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
		m_start = to_point(point);
		m_end	= m_start;
		Invalidate(FALSE);
		return;
	}

	m_drag_target = t;
	if (t == ht_line)
	{
		//라인 평행이동: 라인 중심점을 잡은 것처럼 처리. start 와 마우스의 차이를 기억.
		m_drag_grab_offset = D2D1::Point2F(m_start.x - point.x, m_start.y - point.y);
	}
}

void CSCRulerDlg::on_mouse_move(UINT nFlags, CPoint point)
{
	const double step = snap_step_degrees(nFlags);

	if (m_phase == Phase::phase_place_end && (nFlags & MK_LBUTTON))
	{
		m_end = snap_direction(m_start, to_point(point), step);
		Invalidate(FALSE);
		return;
	}

	if (m_phase == Phase::phase_edit)
	{
		if (m_drag_target == ht_start)
		{
			m_start = snap_direction(m_end, to_point(point), step);
			Invalidate(FALSE);
		}
		else if (m_drag_target == ht_end)
		{
			m_end = snap_direction(m_start, to_point(point), step);
			Invalidate(FALSE);
		}
		else if (m_drag_target == ht_line)
		{
			//라인 전체 평행이동. delta = 새 start - 현재 start.
			const float dx = point.x + m_drag_grab_offset.x - m_start.x;
			const float dy = point.y + m_drag_grab_offset.y - m_start.y;
			m_start = D2D1::Point2F(m_start.x + dx, m_start.y + dy);
			m_end	= D2D1::Point2F(m_end.x	 + dx, m_end.y	 + dy);
			Invalidate(FALSE);
		}
	}
}

void CSCRulerDlg::on_mouse_up(UINT /*nFlags*/, CPoint point)
{
	if (m_phase == Phase::phase_place_end)
	{
		if (dist_sq(to_point(point), m_start) < 4.0)
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
	if (m_phase == Phase::phase_place_end && !m_placed && dist_sq(m_start, m_end) < 1e-6)
	{
		//아직 첫 클릭 전 — 안내 문구.
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

			const float w = scaled_f(620.0f);
			const float h = scaled_f(32.0f);
			const float full_w = float(m_virtual_screen.Width());
			const float lx = (full_w - w) * 0.5f;
			const float ly = scaled_f(24.0f);
			D2D1_ROUNDED_RECT rr = { D2D1::RectF(lx, ly, lx + w, ly + h), scaled_f(6.0f), scaled_f(6.0f) };
			d2dc->FillRoundedRectangle(rr, br_back.Get());

			CStringW msg;
			msg.Format(L"줄자: 클릭+드래그로 라인 그리기 → 끝점/라인 드래그로 조정 / %s / ESC=종료", snap_hint_text());
			d2dc->DrawText(msg, UINT32(msg.GetLength()), tf.Get(),
				D2D1::RectF(lx, ly, lx + w, ly + h),
				br_text.Get(),
				D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
		}
		return;
	}

	const D2D1_POINT_2F p_s = m_start;
	const D2D1_POINT_2F p_e = m_end;

	double dx = double(m_end.x) - m_start.x;
	double dy = double(m_end.y) - m_start.y;
	double length = sqrt(dx * dx + dy * dy);

	ComPtr<ID2D1SolidColorBrush> br_line;
	ComPtr<ID2D1SolidColorBrush> br_tick;
	ComPtr<ID2D1SolidColorBrush> br_handle_stroke;
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0x00B894, 1.00f), br_line.GetAddressOf());	//teal
	d2dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.00f), br_tick.GetAddressOf());
	d2dc->CreateSolidColorBrush(D2D1::ColorF(0x00B894, 1.00f), br_handle_stroke.GetAddressOf());

	d2dc->DrawLine(p_s, p_e, br_line.Get(), scaled_f(2.0f));

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

		//눈금 간격은 "몇 픽셀마다" 라는 측정 단위이므로 물리 픽셀 그대로 둔다.
		//눈금 길이는 보이기 위한 크기라 DPI 로 환산한다.
		const int step_minor = 10;
		const int step_mid	= 50;
		const int step_major = 100;
		const int len_minor	= scaled(4);
		const int len_mid	= scaled(7);
		const int len_major	= scaled(11);

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
				d2dc->DrawLine(t.base, t.tip, br_black.Get(), scaled_f(1.5f));

			d2dc->SetTarget(prev_target.Get());
			cmd_ticks->Close();

			ComPtr<ID2D1Effect> blur;
			if (SUCCEEDED(d2dc->CreateEffect(CLSID_D2D1GaussianBlur, blur.GetAddressOf())))
			{
				blur->SetInput(0, cmd_ticks.Get());
				//STANDARD_DEVIATION 픽셀 단위. 1.6 정도면 약 5px 폭의 부드러운 후광.
				blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, scaled_f(1.6f));
				d2dc->DrawImage(blur.Get());
			}
		}
	}

	//Pass 2: 흰 tick 본체.
	for (const auto& t : ticks)
		d2dc->DrawLine(t.base, t.tip, br_tick.Get(), scaled_f(1.0f));

	//길이 + 각도 라벨. 라인 중점에서 수직 방향으로 약간 떨어뜨림.
	{
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

			//각도는 화면 좌표계 (y 아래) 기준 atan2 → 일반 수학 좌표 (y 위) 로 부호 뒤집어 표시.
			double angle_deg = -atan2(dy, dx) * 180.0 / pi;

			//20260904 by claude. 예전엔 앱이 DPI Unaware 라 화면 배율과 무관하게 96 으로 고정해도 맞았다.
			//지금은 Per-Monitor 인식이라 length 가 물리 픽셀이므로, 실제 cm 를 내려면 패널의 물리 DPI 가 필요하다.
			//MDT_RAW_DPI = 패널이 EDID 로 보고한 실제 DPI (배율 설정이 섞인 effective DPI 가 아니다).
			//모니터가 물리 크기를 잘못 보고하면 값이 어긋날 수 있고, 조회 실패 시 96 으로 떨어진다.
			const double dpi = double(win_compat::dpi::for_point(
				CPoint(int(p_s.x) + m_virtual_screen.left, int(p_s.y) + m_virtual_screen.top),
				win_compat::dpi::dpi_raw));
			const double length_cm = length / dpi * 2.54;

			WCHAR text[96];
			swprintf_s(text, L"%.1f px (%.2f cm)   %.1f°", length, length_cm, angle_deg);

			const float lw = scaled_f(250.0f);
			const float lh = scaled_f(28.0f);

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

			float lx = cx + nx * scaled_f(22.0f) - lw * 0.5f;
			float ly = cy + ny * scaled_f(22.0f) - lh * 0.5f;

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

			D2D1_ROUNDED_RECT rr = { D2D1::RectF(lx, ly, lx + lw, ly + lh), scaled_f(6.0f), scaled_f(6.0f) };
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
		const float r = scaled_f(float(handle_radius));
		D2D1_ELLIPSE e = D2D1::Ellipse(p, r, r);
		d2dc->DrawEllipse(e, br_handle_stroke.Get(), scaled_f(1.5f));
	};
	draw_handle(p_s);
	draw_handle(p_e);
}
