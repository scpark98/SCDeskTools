// SCFrozenOverlayDlg.h
// 풀스크린 프리즈 캡처 오버레이 베이스. 창 캡처 / 영역 캡처 / 각도기 / 줄자가 공통으로 쓰는 부분을 모음.
//
// [공통 동작]
//   - 가상 데스크톱(모든 모니터) 전체를 1회 BitBlt(CAPTUREBLT) 로 프리즈 캡처
//   - WS_POPUP + WS_EX_TOPMOST + WS_EX_TOOLWINDOW + WDA_EXCLUDEFROMCAPTURE
//   - 십자 커서 (파생이 query_cursor 로 변경 가능)
//   - SetCapture, ESC = 종료, 우클릭 = 종료 (둘 다 파생이 override 가능)
//   - OnPaint: BeginDraw → frozen.draw → on_overlay_paint(d2dc) → EndDraw → Present
//   - run_modal_loop(): self-message-loop, WM_QUIT 다시 PostQuitMessage 전파
//
// [파생 클래스가 할 일]
//   - 결과를 담을 멤버 / 접근자 정의
//   - on_overlay_paint() 오버라이드해서 자신의 그래픽만 그림 (배경은 베이스가 그려줌)
//   - on_mouse_down/move/up, on_key_down, on_right_click, query_cursor 등을 필요한 만큼 override
//
// [향후 Common 승격 조건]
//   - SCDeskTools 외부에서 풀스크린 오버레이가 필요한 시점.

#pragma once

#include <afxwin.h>
#include "Common/directx/CSCD2Context/SCD2Context.h"
#include "Common/directx/CSCD2Image/SCD2Image.h"

class CSCFrozenOverlayDlg : public CDialog
{
	//파생 클래스들이 IMPLEMENT_DYNAMIC(X, CSCFrozenOverlayDlg) 로 base 의 RUNTIME_CLASS 를 참조하므로
	//베이스에서도 DYNAMIC 선언이 필요. 없으면 IMPLEMENT_DYNAMIC 매크로 전개 시
	//`classCSCFrozenOverlayDlg` 식별자가 base 에 없다는 컴파일 에러 발생.
	DECLARE_DYNAMIC(CSCFrozenOverlayDlg)

public:
	CSCFrozenOverlayDlg();
	virtual ~CSCFrozenOverlayDlg();

	bool		create(CWnd* parent);

	//오버레이가 DestroyWindow 될 때까지 self-message-loop 실행.
	//caller_for_quit_propagation: WM_QUIT 가 도착하면 loop 빠져나오면서 다시 PostQuitMessage.
	//  보통 호출 다이얼로그를 그대로 넘기면 됨 (현재는 사용 안 하지만 시맨틱 명시 위해 인자로 받음).
	void		run_modal_loop(CWnd* caller_for_quit_propagation);

	CRect		get_virtual_screen_rect() const { return m_virtual_screen; }
	HBITMAP		get_frozen_hbitmap()	const { return m_frozen_hbmp; }	//virtual screen 좌상단 = (0,0)

protected:
	CSCD2Context	m_d2;
	CSCD2Image		m_frozen;
	HBITMAP			m_frozen_hbmp = NULL;
	CRect			m_virtual_screen = {};

	void			finish();	//파생이 결과 확정 후 호출. SetCapture 해제 + DestroyWindow.

	//===== 파생이 override 가능한 hook =====
	//배경 위에 자신의 그래픽 그리기. base 가 BeginDraw / EndDraw 와 frozen 배경 그리기를 담당.
	virtual void	on_overlay_paint(ID2D1DeviceContext* d2dc) {}

	//마우스 핸들러 — 기본은 no-op.
	virtual void	on_mouse_down(UINT nFlags, CPoint pt) {}
	virtual void	on_mouse_move(UINT nFlags, CPoint pt) {}
	virtual void	on_mouse_up	(UINT nFlags, CPoint pt) {}

	//우클릭 — 기본은 finish (취소 의미).
	virtual void	on_right_click(CPoint pt) { finish(); }

	//키 — 기본은 ESC 만 finish. 파생이 Enter 등도 처리하려면 override.
	//반환 true = 메시지 소비, dispatch 차단.
	virtual bool	on_key_down(UINT nChar)
	{
		if (nChar == VK_ESCAPE)
		{
			finish();
			return true;
		}
		return false;
	}

	//커서 결정. NULL 반환 = 윈도우 클래스 기본 커서 (십자) 사용.
	//pt 는 overlay client 좌표.
	virtual HCURSOR	query_cursor(CPoint pt) { return NULL; }

	//===== 각도 스냅 — 줄자 / 각도기가 같은 조합키를 쓰도록 여기에 둔다 =====
	//nFlags 는 마우스 메시지의 MK_* 비트. 반환 0 = 스냅 없음.
	//  Shift = 5°, Ctrl = 15°, Shift+Ctrl = 45° (수평 / 수직 / 대각).
	static double			snap_step_degrees(UINT nFlags);

	//anchor→target 방향만 step_degrees 배수로 맞추고 길이는 그대로 둔다. step_degrees 가 0 이면 target 그대로.
	//결과가 실수인 이유: 정수 픽셀로 반올림해 보관하면 그 점의 실제 각도가 스냅값에서
	//최대 atan(0.7/길이) 만큼 어긋나, 마우스를 움직일 때마다 각도 표시와 라인이 미세하게 떨린다.
	static D2D1_POINT_2F	snap_direction(D2D1_POINT_2F anchor, D2D1_POINT_2F target, double step_degrees);

	//조합키 안내 문구 — 두 도구가 같은 문장을 쓰도록.
	static const wchar_t*	snap_hint_text() { return L"Shift=5° / Ctrl=15° / Shift+Ctrl=45° 스냅"; }

private:
	bool			capture_virtual_screen_to_d2();

	afx_msg void	OnPaint();
	afx_msg BOOL	OnEraseBkgnd(CDC* pDC);
	afx_msg void	OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void	OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void	OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void	OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void	OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void	OnSysKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg BOOL	OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	virtual BOOL	PreTranslateMessage(MSG* pMsg);
	//OnOK / OnCancel: 다이얼로그 매니저가 ESC/Enter 를 IDCANCEL/IDOK 로 라우팅해도 finish() 로 통일.
	//run_modal_loop 가 자체 디스패치라 PreTranslateMessage 가 우회되는 환경에서도 안전망.
	virtual void	OnOK()	override { finish(); }
	virtual void	OnCancel()	override { finish(); }

	DECLARE_MESSAGE_MAP()
};
