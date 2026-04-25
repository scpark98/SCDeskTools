
[동작방식]
- 프로그램 시작 시 메인메뉴에도 지원되는 기능들이 표시되고
  트레이에도 아이콘이 등록되고 팝업메뉴를 통해 실행할 수 있다.
  트레이 아이콘은 CSysTrayWnd을 이용한다.
  D:\1.projects_c++\Common\system\SysTrayIcon

[지원 기능]
- 모든 기능은 Direct2D로 구현한다.
- 컬러 피키 (D:\1.projects_c++\Common\CDialog\CSCColorPicker\SCColorPicker)
- 화면 돋보기 (D:\1.projects_c++\Common\CDialog\CSCColorPicker\SCDropperDlg)
- 화면 캡처는 윈도우 또는 사각형 영역을 선택하여 캡처할 수 있고
  캡처 후 해당 이미지를 floating dlg로 띠워준다. 포스트잇처럼.
  그 창은 resize, move, 확대축소가 가능하다.
  D:\1.projects_c++\Common\directx\CSCD2Context와
  D:\1.projects_c++\Common\directx\CSCD2Image를 이용한다.
- 웹컬러 리스트 (D:\1.projects_c++\SCColorTable)까지 포함할지는 보류.

[단계별 구현]
- 우선 트레이 아이콘과 팝업 메뉴를 구현한다.