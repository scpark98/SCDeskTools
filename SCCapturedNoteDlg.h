// SCCapturedNoteDlg.h
// 캡처된 이미지를 포스트잇처럼 띠우는 컨테이너 다이얼로그.
// - WS_POPUP, 캡션바 / 보더 없음, WS_EX_TOOLWINDOW (taskbar 미노출).
// - 자식으로 CSCD2ImageDlg (simple_mode=true) 를 두어 이미지 표시 + 줌(휠) + 팬(드래그) 위임.
// - 가장자리 8px = resize 핸들 (OnNcHitTest).
// - 일반 드래그 = 창 이동 (HTCAPTION 트리거).
// - Shift + 드래그 = 이미지 pan (simple_mode 자식이 자체 pan 을 안 하므로 NoteDlg 가 m_img_dlg.scroll() 직접 호출).
// - 휠 = 줌 인/아웃 (m_img_dlg.zoom(±1)).
// - 가운데 버튼 클릭 = 크기 / 비율 / 마우스 픽셀 좌표 표시 토글.
// - +, - 키 = 줌 인/아웃.
// - 우클릭 = 컨텍스트 메뉴 (클립보드 복사 / 100% / fit / 배경색 / 닫기).
// - ESC = 닫기.
// - heap 할당 + PostNcDestroy 에서 self-delete (멀티 인스턴스 안전).
//
// [향후 Common 승격 조건]
//   - 캡처 외 다른 소스 (드래그&드롭 이미지, 이미지 파일 미리보기 등) 에서도 재사용 발생 시.

#pragma once

#include <afxwin.h>
#include <vector>
#include "Common/directx/CSCD2Context/SCD2Context.h"
#include "Common/directx/CSCD2Image/SCD2Image.h"
#include "Common/CDialog/SCD2ImageDlg/SCD2ImageDlg.h"

//CSCD2ImageDlg 의 simple_mode 는 OnLButtonDown/Move/Up 을 base 위임만 하므로
//마우스 이벤트가 dispatch 단계에서 효과적으로 무시된다. 우리가 derived 에서 override 하여
//자식 자체의 표준 dispatch 로 마우스를 받아 pan / 창 이동을 처리한다.
//(NoteDlg 의 PreTranslateMessage 가 자식 영역 마우스를 가로채는 데 의존하지 않음.)
class CSCCapturedNoteDlg : public CDialog
{
	DECLARE_DYNAMIC(CSCCapturedNoteDlg)

public:
	CSCCapturedNoteDlg();
	virtual ~CSCCapturedNoteDlg();

	//[heap 할당 + 자체 lifecycle]
	//bgra_top_down: 32bpp BGRA 픽셀, top-down 행 순서. 내부에서 D2D 비트맵으로 복사하므로 호출자는 이후 free 가능.
	//pos_screen:    창 시작 위치 (보통 캡처된 윈도우의 원래 좌상단). NULL 이면 화면 중앙.
	//반환값:         성공 시 다이얼로그 포인터 (소유권은 다이얼로그 자신, self-delete). NULL 이면 실패.
	static CSCCapturedNoteDlg* spawn(const BYTE* bgra_top_down, int w, int h, const POINT* pos_screen = NULL);

private:
	enum
	{
		edge_resize = 8,	//창 가장자리 N px = resize 영역
		cmd_copy = 1,
		cmd_zoom_100 = 2,
		cmd_zoom_fit = 3,
		cmd_close = 4,
		cmd_save = 5,
		cmd_gradient_edge = 6,
		cmd_back_default = 7,
		cmd_back_zigzag = 8,
		cmd_back_custom = 9,
		cmd_interp_nearest = 10,
		cmd_interp_linear = 11,
	};

	CSCD2Context	m_d2;
	CSCD2Image		m_image;
	CSCD2ImageDlg	m_img_dlg;	//ASee 와 동일 패턴 — derived 없이 직접 사용. enable_pan 만 켬.

	CPoint			m_pt_mouse;

	int				m_img_w = 0;
	int				m_img_h = 0;
	bool			m_initialized = false;
	BYTE			m_alpha = 255;	//Ctrl+wheel 로 조정. 64 ~ 255 범위 (완전 투명 방지).

	//우상단 닫기 버튼 — child window 없이 m_img_dlg 의 post_paint 콜백에서 D2D 로 직접 그림.
	//배경 투명 문제 회피: round 코너 바깥은 그냥 안 그려 이미지/letterbox 가 자연스럽게 비침.
	bool			m_close_btn_hover = false;	//마우스가 버튼 영역 위 — 호버 시점에만 가시화
	bool			m_close_btn_pressed = false;	//LButtonDown 으로 시작된 클릭 진행 중

	bool			m_show_info = true;			//크기 / 비율 / 마우스 픽셀 좌표 표시. 가운데 버튼으로 토글, 레지스트리 유지

	//20260904 by claude. **이 창은 DPI 로 스케일하지 않는다.**
	//client 가 이미지 픽셀과 1:1 이라 320px 캡처는 어느 배율의 모니터에서도 320px 이다.
	//그 고정된 판 위에 그리는 크롬(닫기 버튼 / 리사이즈 여백 / 정보 문자열)만 DPI 를 따라 커지면
	//비율이 깨진다 — 175% 에서 버튼이 노트 폭의 11.6%, 100% 에서는 6.6% 가 된다.
	//메인 다이얼로그는 창 자체가 DPI 로 커지므로 버튼도 커지는 것이 맞고, 여기는 반대다.
	//따라서 노트 안의 모든 크기는 상수 그대로 쓴다.
	//
	//WM_DPICHANGED 는 처리하지 않는다 — OS 기본 동작(배율비로 창 축소)이 맞다.
	//175% 에서 캡처한 그림은 그 배율로 렌더된 결과라, 100% 모니터에서는 그 배율만큼 줄여 보여줘야
	//그 모니터에서 원래 보였을 크기가 된다. 한때 "이미지는 1:1 이어야 한다" 며 크기를 되돌리는
	//코드를 넣었다가, 100% 모니터에서 확대되어 보이는 회귀를 만들어 되돌렸다.

	//원본 BGRA 픽셀 보관본. 반복 가능한 효과 (gradient edge 등) 적용 시 매번 reload.
	//m_image 의 m_data 와 별개 — m_image 는 D2D 비트맵 캐시이므로 픽셀 수정 후 load() 다시 호출 필요.
	std::vector<BYTE>	m_bgra_data;
	bool				m_edge_padded = false;	//gradient edge 첫 호출 시 마진 1회 확장 후 true.

	//마우스 호버 위치의 이미지 픽셀 좌표. (-1, -1) = 이미지 영역 밖이거나 미초기화.
	//OnNcMouseMove 에서 갱신 (OnNcHitTest 가 client 영역을 HTCAPTION 으로 반환하므로 일반 이동도 NC 경로로 옴).
	Gdiplus::PointF	m_hover_pixel = Gdiplus::PointF(-1.0f, -1.0f);

	bool			init_with_image(const BYTE* bgra, int w, int h, const POINT* pos_screen);

	//이미지를 100% 로 보여줄 client 크기를 구한다. 기준 모니터의 2/3 를 넘으면 비율을 유지한 채 줄인다.
	//반환값 = 그 크기에 대응하는 표시 배율 (1.0 = 100%).
	double			calc_client_size_for_image(const CRect& rc_monitor, int& cx, int& cy) const;
	void			apply_display_scale(double scale);
	//client 가 정확히 cx x cy 가 되도록 창을 리사이즈. pos_client_screen 을 주면 client 좌상단을 그 좌표에 둔다.
	void			resize_client_to(int cx, int cy, const POINT* pos_client_screen);
	void			show_context_menu(CPoint pt_screen);
	void			apply_back_setting(DWORD value);	//back_default / back_zigzag / ARGB 를 m_img_dlg 에 반영
	DWORD			get_back_setting() const;			//현재 상태를 같은 표현으로 되돌려줌 (메뉴 체크 표시용)
	void			execute_cmd(int cmd);	//메뉴 항목과 단축키가 공유하는 명령 디스패처
	void			on_img_dlg_post_paint(ID2D1DeviceContext* d2dc);	//m_img_dlg 의 D2D frame 안에서 추가 오버레이 그리기
	CRect			get_close_button_rect() const;	//닫기 버튼 client 좌표 rect (OnSize / OnNcHitTest / paint / click 공유)

	afx_msg void	OnSize(UINT nType, int cx, int cy);

	//OnNcHitTest 가 대부분의 client 영역을 HTCAPTION 으로 돌려주므로 가운데 클릭은 NC 경로로 온다.
	//닫기 버튼 영역만 HTCLIENT 라 그쪽은 OnMButtonDown 이 받는다. 둘 다 toggle_info() 로 모인다.
	afx_msg void	OnNcMButtonDown(UINT nHitTest, CPoint point);
	afx_msg void	OnMButtonDown(UINT nFlags, CPoint point);
	void			toggle_info();
	afx_msg LRESULT	OnNcHitTest(CPoint point);
	afx_msg void	OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
	afx_msg void	OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void	OnNcRButtonUp(UINT nHitTest, CPoint point);
	virtual BOOL	PreTranslateMessage(MSG* pMsg);
	virtual void	OnOK();
	virtual void	OnCancel();
	virtual void	PostNcDestroy();

	DECLARE_MESSAGE_MAP()
public:
	afx_msg BOOL OnNcActivate(BOOL bActive);
	afx_msg void OnNcMouseMove(UINT nHitTest, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
};
