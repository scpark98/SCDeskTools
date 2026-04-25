// SCCapturedNoteDlg.h
// 캡처된 이미지를 포스트잇처럼 띠우는 컨테이너 다이얼로그.
// - WS_POPUP, 캡션바 / 보더 없음, WS_EX_TOOLWINDOW (taskbar 미노출).
// - 자식으로 CSCD2ImageDlg (simple_mode=true) 를 두어 이미지 표시 + 줌(휠) + 팬(드래그) 위임.
// - 가장자리 8px = resize 핸들 (OnNcHitTest).
// - 일반 드래그 = 창 이동 (HTCAPTION 트리거).
// - Shift + 드래그 = 이미지 pan (simple_mode 자식이 자체 pan 을 안 하므로 NoteDlg 가 m_img_dlg.scroll() 직접 호출).
// - 휠 = 줌 인/아웃 (m_img_dlg.zoom(±1)).
// - +, - 키 = 줌 인/아웃.
// - 우클릭 = 컨텍스트 메뉴 (클립보드 복사 / 100% / fit / 닫기).
// - ESC = 닫기.
// - heap 할당 + PostNcDestroy 에서 self-delete (멀티 인스턴스 안전).
//
// [향후 Common 승격 조건]
//   - 캡처 외 다른 소스 (드래그&드롭 이미지, 이미지 파일 미리보기 등) 에서도 재사용 발생 시.

#pragma once

#include <afxwin.h>
#include "Common/directx/CSCD2Context/SCD2Context.h"
#include "Common/directx/CSCD2Image/SCD2Image.h"
#include "Common/CDialog/SCD2ImageDlg/SCD2ImageDlg.h"

//CSCD2ImageDlg 의 simple_mode 는 OnLButtonDown/Move/Up 을 base 위임만 하므로
//마우스 이벤트가 dispatch 단계에서 효과적으로 무시된다. 우리가 derived 에서 override 하여
//자식 자체의 표준 dispatch 로 마우스를 받아 pan / 창 이동을 처리한다.
//(NoteDlg 의 PreTranslateMessage 가 자식 영역 마우스를 가로채는 데 의존하지 않음.)
class CSCNoteImageDlg : public CSCD2ImageDlg
{
	DECLARE_DYNAMIC(CSCNoteImageDlg)

public:
	using CSCD2ImageDlg::CSCD2ImageDlg;

protected:
	//pan only — 창 이동은 부모 OnNcHitTest 가 HTCAPTION 으로 반환해 Windows 가 처리.
	bool	m_panning = false;
	POINT	m_pan_last = {};

	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	DECLARE_MESSAGE_MAP()
};


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
		kEdgeResize = 8,	//창 가장자리 N px = resize 영역
		kCmdCopy = 1,
		kCmdZoom100 = 2,
		kCmdZoomFit = 3,
		kCmdClose = 4,
	};

	CSCD2Context	m_d2;
	CSCD2Image		m_image;
	CSCNoteImageDlg	m_img_dlg;	//derived: pan/창이동 을 자식 자체에서 처리

	int				m_img_w = 0;
	int				m_img_h = 0;
	bool			m_initialized = false;

	bool			init_with_image(const BYTE* bgra, int w, int h, const POINT* pos_screen);
	void			show_context_menu(CPoint pt_screen);

	afx_msg void	OnSize(UINT nType, int cx, int cy);
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
};
