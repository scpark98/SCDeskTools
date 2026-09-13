@../Common/claude.md

# SCDeskTools 프로젝트 컨텍스트

데스크톱 유틸리티 모음 (캡처 / 컬러피커 / 돋보기 / 줄자 / 각도기 / 캡처 노트). 메인 트레이 아이콘 기반 + 메인 다이얼로그에 즐겨찾기 툴 버튼 노출. PicPick / 알캡처 / 칼무리 류의 한국형 데스크톱 도구를 직접 만들기 위한 개인 도구.

설계 의도·기능 인벤토리·우선순위 후보는 `readme.txt` 참조 (이 파일과 함께 git 동기화). 본 `claude.md` 는 readme 에 없는 **결정사항 / 선택 이유 / 미기록 컨텍스트** 만 담는다.

---

## 1. 아키텍처 결정

### 1.1 메인 다이얼로그 = 트레이 + 즐겨찾기 버튼

- 진입은 `CSCDeskToolsDlg` (`SCDeskToolsDlg.h:14`).
- `m_sys_tray` (`Common/system/SysTrayIcon`) 가 우클릭 팝업 + 더블클릭 토글 담당.
- `m_favorites` (vector<UINT>) + `m_buttons_favorite` (동적 생성 CButton) 으로 메인 창에 자주 쓰는 툴 노출. 현재는 `OnInitDialog` 의 `kDefaultFavorites` 로 하드코딩, **향후 설정창 → 레지스트리 저장** 으로 확장 예정.
- 우측 하단 `m_button_exit` 는 일반 종료 버튼. 현재는 `WS_VISIBLE` 제거로 숨김. 트레이 종료 절차가 번거로울 때 도로 노출 가능 — `build_exit_button` 의 Create 스타일에 `WS_VISIBLE` 추가 또는 `m_button_exit.ShowWindow(SW_SHOW)` 한 줄.

### 1.2 그래픽 구현 = Direct2D 통일 (readme 명시)

캡처 결과 표시·오버레이·이미지 다이얼로그 모두 D2D. GDI/GDI+ 는 buttons 등 컨트롤 픽셀 그리기에서만. Common 의 `CSCD2Context` / `CSCD2Image` / `CSCD2ImageDlg` 를 광범위하게 사용.

### 1.3 풀스크린 오버레이 베이스 — `CSCFrozenOverlayDlg`

`SCFrozenOverlayDlg.h:26` 가 베이스. 가상 데스크톱 1회 BitBlt(`CAPTUREBLT`) 프리즈 → topmost popup + `WS_EX_TOOLWINDOW` + `WDA_EXCLUDEFROMCAPTURE` → self-message-loop.

파생:
- `CSCCaptureOverlayDlg` — 창 캡처 (마우스 아래 top-level window rect)
- `CSCRegionCaptureDlg` — 영역 캡처 (드래그 사각형, 외부 마스크)
- `CSCRulerDlg` — 줄자 (PlaceEnd / Edit phase, Shift = 0/45/90° 스냅)
- `CSCProtractorDlg` — 각도기 (PlaceArmA / PlaceArmB / Edit 3-phase)

파생은 `on_overlay_paint`, `on_mouse_*`, `on_key_down`, `query_cursor` 만 override.

**Why frozen 방식**: 라이브 화면 위에 오버레이를 띄우면 마우스/UI 가 계속 변해 측정이 흔들림. 1회 캡처로 freeze 하면 측정·드래그 중 화면이 정지 — UX 안정성 + 구현 단순화.

**향후 Common 승격 조건**: SCDeskTools 외부에서도 풀스크린 오버레이가 필요해질 때.

### 1.4 캡처 결과 표시 = `CSCCapturedNoteDlg`

`SCCapturedNoteDlg.h:29`. 포스트잇 스타일 floating dialog. 자식으로 `CSCD2ImageDlg(simple_mode=true)` 를 두고 zoom·pan·렌더링 위임.

- `WS_POPUP` + 캡션/보더 없음 + `WS_EX_TOOLWINDOW` (taskbar 미노출).
- 가장자리 `kEdgeResize=8` px = resize 핸들 (`OnNcHitTest`).
- 일반 드래그 = 창 이동 (HTCAPTION 트리거).
- Shift + 드래그 = pan (simple_mode 자식이 자체 pan 안 하므로 NoteDlg 가 `m_img_dlg.scroll()` 직접 호출).
- 휠 = zoom, +/- 키 = zoom, Ctrl+휠 = alpha (64~255, 완전 투명 방지).
- 우클릭 = 컨텍스트 메뉴 (copy / 100% / fit / save / close).
- ESC = 닫기.
- **heap 할당 + `PostNcDestroy` self-delete** → 멀티 인스턴스 안전. `spawn()` 이 유일한 생성 경로.

`CSCD2ImageDlg::simple_mode` 가 마우스 이벤트를 base 위임만 하므로 NoteDlg 가 자식 위 마우스를 dispatch 단계에서 자연히 받음 — `PreTranslateMessage` 로 가로채는 식 회피.

**향후 Common 승격 조건**: 캡처 외 다른 소스(드래그&드롭, 파일 미리보기 등) 에서 재사용 발생 시.

### 1.5 클립보드 트래킹 = `WM_CLIPBOARDUPDATE` (Vista+ listener)

`AddClipboardFormatListener` 사용. 구식 `SetClipboardViewer` 체인 방식과 달리 체인 끊김 위험 없음. `update_paste_clipboard_state()` 가 메뉴/버튼 enable 갱신.

### 1.6 트레이 복구 — `TaskbarCreated` 메시지

`Message_TaskbarCreated` (`RegisterWindowMessage("TaskbarCreated")`). 시작프로그램으로 실행 시 Shell_TrayWnd 가 늦게 올라오는 경우 + Explorer 크래시 후 재시작 모두 동일 핸들러로 복구.

### 1.7 글로벌 단축키 — `RegisterHotKey` + `WM_HOTKEY`

트레이 상주 앱이라 메인 창이 hidden 인 상태에서도 단축키가 동작해야 한다. `WH_KEYBOARD_LL` 같은 시스템 hook 은 AV/EDR 의심 + 응답성 부담이 커서 채택 안 함. Win32 표준 `RegisterHotKey` 가 충분.

**구현 형태** (`SCDeskToolsDlg.cpp` 익명 namespace):
```cpp
struct HotkeyItem
{
    int       id;          //RegisterHotKey ID (per-window 1~0xBFFF)
    UINT      tool_id;     //ID_TOOL_* — WM_COMMAND 으로 dispatch
    UINT      modifiers;   //MOD_ALT | MOD_SHIFT | ... (MOD_NOREPEAT 권장)
    UINT      vkey;
    LPCTSTR   description; //등록 실패 알림용
};
const HotkeyItem kHotkeys[] = { ... };
```

`OnInitDialog` 에서 일괄 `RegisterHotKey` → 실패한 것만 풍선 알림으로 묶음 보고. `OnDestroy` 에서 `UnregisterHotKey`. `WM_HOTKEY` 핸들러는 `wParam` (= HotkeyItem.id) 으로 tool_id 찾아 `SendMessage(WM_COMMAND, tool_id)` — 기존 `ON_COMMAND` 핸들러를 그대로 재사용.

**왜 `SendMessage(WM_COMMAND, ...)` 로 위임하나**: ON_COMMAND 핸들러가 이미 메뉴/버튼/단축키의 단일 진입점이 되도록 구현돼 있어, 단축키 dispatch 도 동일 경로로 통일하면 향후 enable/disable 상태 동기화·로깅이 한 곳에 모임.

**MOD_NOREPEAT**: 키 누르고 있을 때 OS auto-repeat 으로 WM_HOTKEY 가 연발하면 캡처 오버레이가 겹쳐 뜨는 것 같은 사고가 남. 토글성·1회성 액션은 항상 같이 둔다.

**충돌**: `RegisterHotKey` 가 `FALSE` 면 `GetLastError()=ERROR_HOTKEY_ALREADY_REGISTERED`. 다른 앱이 같은 조합을 먼저 잡고 있다는 의미라 사용자가 다른 조합으로 변경할 수 있게 향후 설정 다이얼로그 필요(§5 todo).

**현재 등록된 조합** (코드 `kHotkeys` 참조):
- `Alt+Shift+S` → 창 캡처 (`ID_TOOL_CAPTURE_WINDOW`)
- `Alt+Shift+F` → 전체 화면 캡처 (`ID_TOOL_CAPTURE_FULLSCREEN`)
- `Alt+Shift+R` → 영역 캡처 (`ID_TOOL_CAPTURE_REGION`)
- `Alt+Shift+V` → 클립보드 이미지 띠우기 (`ID_TOOL_PASTE_CLIPBOARD`)
- `Alt+Shift+C` → 컬러 피커 (`ID_TOOL_COLOR_PICKER`)
- `Alt+Shift+M` → 화면 돋보기 (`ID_TOOL_DROPPER`)
- `Alt+Shift+P` → 각도기 (`ID_TOOL_PROTRACTOR`)
- `Alt+Shift+L` → 줄자 (`ID_TOOL_RULER`)
- `Alt+Shift+T` → 클립보드 이미지 텍스트 인식 (`ID_TOOL_OCR_CLIPBOARD`)
- `Alt+Shift+1`..`9` → 1번..N번 모니터 캡처 (`ID_TOOL_CAPTURE_MONITOR_FIRST + i`) — **동적**

**동적 단축키 (모니터)**:
- `kHotkeys` 와 ID 충돌 피하려고 `kMonitorHotkeyIdBase = 100` 부터 사용. `i` 번째 모니터 = ID `100+i`.
- 단일 모니터 환경에서는 등록 생략 (전체 화면 캡처와 같으므로 단축키 낭비). 2개 이상일 때만 활성화.
- **WM_DISPLAYCHANGE** (`on_display_change`) 에서 핫플러그·해상도 변경 감지 → `unregister_monitor_hotkeys` → `enum_display_monitors` → `register_monitor_hotkeys` 순으로 재등록. 정적 `kHotkeys` 는 디스플레이 무관이라 손대지 않음.
- `m_registered_monitor_count` 로 직전에 등록된 ID 범위 추적. UnregisterHotKey 때 정확히 그 범위만 푸는 데 사용.

---

## 2. Common 라이브러리 의존 (이 프로젝트 한정)

전역 규칙(§2 in `Common/claude.md`) 위에, 이 프로젝트에서 실제 쓰는 모듈 목록:

- `Common/system/SysTrayIcon/SysTrayIcon.h` — 트레이 아이콘 + 팝업.
- `Common/CDialog/CSCColorPicker/SCColorPicker.h` — modeless 컬러 피커. `OnInitDialog` 에서 1회 create, 결과는 `WM_USER` 류 메시지로 수신 (`on_message_CSCColorPicker`).
- `Common/CDialog/CSCColorPicker/SCDropperDlg` — 화면 돋보기.
- `Common/directx/CSCD2Context` + `CSCD2Image` + `CDialog/SCD2ImageDlg` — D2D 컨텍스트 / 비트맵 / 이미지 다이얼로그.
- `Common/CButton/GdiButton/GdiButton.h` — 노트 우상단 닫기 버튼.

**원칙**: 이 프로젝트 폴더만 grep 해서 "기능 없다" 라고 단정 금지. `m_xxx.foo()` 이면 먼저 `m_xxx` 의 타입 확인 → Common 추적.

---

## 3. 기능 진행 상태 (2026-04-26 기준 두 번째 커밋 `8542db2`)

readme.txt 의 기능 인벤토리 중 **실제로 코드에 들어간 항목** 만 마킹:

### 구현 완료
- 트레이 아이콘 + 팝업 메뉴 + 메인 다이얼로그 토글
- 컬러 피커 (modeless, Common)
- 화면 돋보기 (Common SCDropperDlg)
- **창 캡처** (`CSCCaptureOverlayDlg`)
- **영역 캡처** (`CSCRegionCaptureDlg`)
- **전체 화면 캡처** + **모니터별 캡처** (`OnToolCaptureFullscreen`, `OnToolCaptureMonitor` ID range)
- **클립보드에서 노트 띄우기** (`OnToolPasteClipboard`, 텍스트도 그대로 이미지화)
- **줄자** (`CSCRulerDlg`)
- **각도기** (`CSCProtractorDlg`)
- **캡처 노트** = floating dialog (zoom / pan / alpha / save / copy)
- **캡처 노트 텍스트 인식(OCR)** — 우클릭 메뉴 `텍스트 인식`. 결과 텍스트만 클립보드에 넣는다
- 메인 창 즐겨찾기 버튼 (하드코딩)
- **글로벌 단축키** (`RegisterHotKey` 베이스, `kHotkeys` 테이블) — 현재 `Alt+Shift+S` = 창 캡처

### readme 후보 중 미구현
- 마지막 영역 재캡처 / 고정 크기 영역 / 자유 형태 / 스크롤 캡처 / 딜레이 캡처
- 크로스헤어 / 격자 / 가이드 라인
- 색상 팔레트 / contrast / 그라디언트
- 노트 위 주석·낙서 / 노트 잠금
- 화면 녹화 / 화이트보드 / 항상 위 토글 / 블루라이트 필터 / 창 정보 표시 / DPI 정보

readme 의 "즉시 추가 권장" 셋 중 **노트 파일 저장 (15)** 만 구현됨 (`kCmdSave`). 나머지(3·1·7·8·21) 는 다음 단계 후보.

---

## 4. 결정 / 선택 이유 (비자명한 것만)

### 4.1 종료 버튼은 숨김 유지 (제거 X)

`m_button_exit` 는 우측 하단에 만들어두지만 `WS_VISIBLE` 빼서 숨김 상태. 코드/위치 보존 → 추후 필요 시 한 줄 수정만으로 도로 노출. 평소 종료 흐름은 트레이 우클릭 → 종료 / Alt+F4 → 트레이 숨김 / 트레이 → 종료.

### 4.2 `HideFloating` RAII

4개 캡처 핸들러(`OnToolCaptureWindow/Region/Fullscreen/Monitor`) 가 모두 "메인+피커 숨김 → wait → overlay → 복원" 패턴이라 RAII destructor 로 추출. early return / exception 어느 경로에서도 자동 복원.

### 4.3 캡처 노트 self-delete

`spawn()` 으로만 생성, `PostNcDestroy` 에서 `delete this`. 호출자(`CSCDeskToolsDlg`) 가 lifecycle 신경 쓸 필요 없음 + 동시에 여러 노트 떠 있을 수 있음.

### 4.4 노트 파일 저장 = recent_folder 패턴 (Common 규칙 §7)

`SCCapturedNoteDlg` 의 `kCmdSave` (저장 메뉴) 는 반드시 `Common/claude.md §7` 의 read → use → write-back 패턴을 따른다. 2026-04-27 사용자 철칙으로 등록됨. 이 프로젝트의 모든 향후 파일 저장/열기 다이얼로그도 동일.

### 4.5 캡처 노트 OCR — 캡처 시점이 아니라 우클릭 메뉴에서

인식 엔진은 Common 공통모듈 `Common/ocr/SCWinOcr` (Windows.Media.Ocr, OS 내장).
외부 lib·traineddata 없음. `.vcxproj` 는 그 `.cpp` 하나만 `stdcpp20` (C++/WinRT 요구, 프로젝트 전체는 stdcpp17).

- **캡처할 때는 OCR 하지 않는다.** 동기 블로킹이라 노트가 뜨는 시점이 밀린다.
- **클립보드에는 인식 텍스트만 넣는다.** 이미지는 캡처 시점에 이미 들어가 있고,
  OCR 메뉴를 고른 행위 자체가 "지금은 텍스트가 목적" 이라는 뜻이다.
- **회전은 자동 감지한다.** 엔진에 자동 회전 보정이 없어(실측: 책등 세로 글자가 0도에서 한 글자도
  안 나오다가 90도에서 전부 나옴), 0/90/270 을 각각 한 번씩 넣어 **글자답게 읽힌 문자 수**가 가장
  많은 방향을 고른 뒤 그 방향으로 본 인식을 한다. 동점이면 0도를 유지한다.
  "글자답다" 는 한글/영숫자/일반 문장부호 비율 80% 이상 (`looks_like_text`) — 엉뚱한 방향은
  한자·괘선·희귀기호를 뱉는다 (실측: `1러Y8|( 4고께z 目들 日十07`).
  회전은 픽셀 이동일 뿐이라 색이 달라지지 않으므로, 색·크기는 그 방향 버퍼에서 그대로 뽑는다.
- **세로쓰기(글자를 한 자씩 쌓은 배치)는 지원 범위 밖이다.** -105~105도를 5도 간격으로, 배율
  0.22~1.5 까지 전부 시도해도 한 글자도 나오지 않았다. 회전된 글줄이 아니라 배치 자체가 다르고,
  이 엔진에는 세로쓰기 모드가 없다.
- 입구는 둘, 경로는 하나다. 노트 위 **Ctrl+T**(또는 우클릭 메뉴)와 전역 **Alt+Shift+T**(클립보드의
  마지막 이미지 대상)가 모두 `CSCDeskToolsDlg::run_ocr_on_bgra()` 를 탄다. 캡처 노트를 띄우지 않는
  설정에서도 인식할 수 있어야 해서 전역 단축키를 뒀다 — 캡처하면 이미지가 곧바로 클립보드에 들어간다.
  클립보드 읽기는 `read_clipboard_image_bgra()` 로 `클립보드 이미지 띠우기`(Alt+Shift+V)와 공유한다.
- **클립보드에는 RTF(`Rich Text Format`) + 평문을 함께 넣는다.** 엔진은 색도 글자 크기도 알려주지 않으므로,
  단어 상자(`SCOcrWord`)로 원본 픽셀을 되짚어 색을 뽑고 상자 높이로 크기를 환산한다.
  글자색은 `\cf`, 배경색은 `\highlight` 로 싣는다 — 어두운 테마를 캡처했을 때 흰 글자가 흰 문서 배경에
  묻히지 않게 하려면 배경도 같이 실어야 한다. 토스트에는 같은 값을 `<cr=#RRGGBB><sz=n>` 태그로 표시한다.
- **토스트 가독성은 두 겹으로 보장한다.** 패널색을 고정 어두운색으로 두고 글자색만 원본에서
  가져오면, 원본이 검정 글자였을 때 어두운 패널 위 검정 글자가 되어 보이지 않는다 (실제 사고).
  (1) 패널색을 단어 배경들의 최빈색(`get_dominant_back_color`)으로 깔아 **원본에 이미 있던 대비를
  그대로 재현**하고, (2) 그래도 모자라면 `colors.h` 의 `get_readable_text_color(패널, 원본색, 4.5)` 가
  WCAG 4.5 통과 시 원본 색을 유지하고 못 넘을 때만 흑/백으로 바꾼다. 색은 최대한 살고 가독성은 보장된다.
  RTF 쪽은 `\cf` + `\highlight` 로 원본 쌍을 그대로 실으므로 이 보정을 적용하지 않는다 (충실도 우선).
- **한 단어가 한 색이라는 제약은 없다.** 엔진이 단어 단위로만 돌려줄 뿐이고, 실제로는 한 단어 안에서
  색이 바뀐다 (`빙수가` = 핑크 `빙수` + 크림 `가`, `빚까지` = 연두 `빚` + 주황 `까지`).
  단어당 색 하나만 뽑으면 밝은 쪽이 이겨 핑크가 크림색으로 둔갑했다.
  `split_word_by_color` 가 열별 잉크 색으로 구간을 나눈 뒤 글자를 폭 비율로 배분해 조각낸다
  (조각은 `ocr_hit::glued` 로 공백 없이 이어 붙인다).
  - 임계값은 실측이다 — 같은 색 안의 열 간 편차 ≤41, 다른 색 사이 ≥92 로 뚜렷이 갈려 그 사이인 64 를 쓴다.
  - 오분할을 막는 장치가 셋이다. (1) 획이 얇게 스치는 열(잉크 픽셀 < 높이/6)은 기준에서 뺀다
    (2) 비교 기준은 구간의 *평균* 색 — 시작 열 하나로 삼으면 어두운 가장자리에서 갈라진다
    (3) 마지막에 이웃 구간의 대표색이 가까우면 도로 합친다. 셋을 다 넣고서야 `아이고`(단색)가
    3조각에서 1조각이 됐다.
  - **토스트도 단어별 배경을 그린다** (`<crb=#RRGGBB>`). 추출만 고치고 표시를 안 고치면 화면상 결과가
    그대로라 "안 고쳐졌다" 로 보인다 — 실제로 그랬다. 패널과 색이 같으면 태그를 내지 않는다:
    run 박스는 불투명이라 반투명 패널 위에 같은 색을 덧칠하면 그 단어만 진해져 얼룩이 된다.
    글자색 대비 판정도 "실제로 깔릴 배경"(자기 배경 또는 패널) 기준으로 한다.
  - **배경색도 조각마다 다시 뽑는다.** 단어 전체의 배경을 물려주면 한 단어 안에서 배경이 바뀌는 경우를
    놓친다 — `할아버지가` 에서 `할아` 에만 깔린 하늘색 형광이 통째로 사라졌다 (전체 배경이 흰색으로 잡힘).
    조각 바깥 링의 위/아래 띠가 링 픽셀의 대부분이라 조각 단위로도 안정적이다
    (실측: `할`/`아` = #9AC6E9, `버`/`지`/`가` = #F9F9F9).
  - **열 중앙값을 그 열의 배경으로 보고 배경 변화로 쪼개는 방식은 쓰지 않는다.** 시도했다가 버렸다 —
    굵은 세로획이 있는 열은 중앙값이 잉크색이 되어 한 단어가 26조각으로 쪼개졌다.
    배경만 다르고 글자색이 같은 경우도 실제로는 안티에일리어싱이 배경 쪽으로 번져 잉크 색이 달라지므로
    지금의 잉크 기반 분할로 잡힌다.
  - **[가정]** 한 단어 안의 글자 폭은 고르다 — 구간 경계가 몇 번째 글자인지를 폭 비율로 정하기 때문이다.
    한글은 잘 맞는다(실측: 빙수가 0.69 vs 2/3, 빚까지 0.31 vs 1/3). 라틴 문자처럼 폭이 들쭉날쭉하면
    경계가 한 글자 어긋날 수 있다. 색이 하나뿐인 단어는 영향이 없다.
- **글자색·배경색은 "단어 상자 바깥 테두리 = 배경" 으로 잡는다.** 상자 *안*의 최빈색을 배경으로 보던
  방식으로 되돌리지 말 것 — 2026-09-12 에 그렇게 했다가 크고 굵은 글자에서 잉크가 상자의 절반을
  넘어 **글자와 배경이 통째로 뒤바뀌었다** (실측: `150매` 가 글자=#D2CCCA 배경=#221D1B).
  상자 바깥은 잉크 비율과 무관하게 배경이므로 이 문제가 원리적으로 생기지 않는다.
  글자색은 그 배경에서 밝기가 가장 먼 색 — 안티에일리어싱 중간 톤이 많아 "최빈" 이 아니라 "최원" 이
  실제 잉크다. 점 하나짜리 노이즈를 피하려고 상자 면적의 1%(최소 4px) 이상인 색만 후보로 둔다.
  대비가 24 미만이면 `get_readable_text_color(배경)` 으로 흑/백 중 읽히는 쪽을 쓴다.
- **글자 크기 환산 근거**: 단어 상자 높이 ≈ 글자 px × 0.92 (11~38px 합성 샘플 실측).
  pt = (높이 / 0.92) × 72 / 캡처 DPI.

### 4.5.1 인식률 — 실측으로 확인된 것 (2026-09-12)

합성 샘플(맑은 고딕, 흰 배경/검정 글자, ClearType)로 잰 결과. **글자 높이가 지배적 변수다.**

| 글자 높이 | x1 결과 | 비고 |
|---|---|---|
| 11~16px | 심하게 깨짐 | 화면 100% 배율의 보통 UI 글자가 여기 해당 |
| 20px | 거의 정확 | |
| 24px 이상 | 다시 나빠지기 시작 | |
| 38px 자막체(굵은 검정 외곽선) | 숫자열이 통째로 깨짐 | **축소(x0.5)했을 때 가장 정확했다** |

- 작으면 확대가 크게 듣는다 (12px 는 x1 에서 문장이 무너지는데 x2 면 거의 맞는다).
- **크면 축소가 듣는다.** 확대는 오히려 나빠진다.
- **재샘플링 자체가 손해다.** 이미 정확하던 20px 를 x1.11 로 살짝 키우자 오히려 틀렸다.
  배율을 건드릴 거면 정수배가 안전하다.
- 이진화(Elysium 방식 다중 밴드)는 자막체에서 도움이 되지 않았다.
- 인식기 언어는 이 머신에서 `ko` 가 선택되는 것을 확인했다 (`ja` 도 설치, `en` 은 없음). 언어 선택 문제는 아니다.

**앱 경로는 깨끗하다 (로그로 확인, 2026-09-12).** `alpha=255~255`(전면 투명 사고 없음),
인식기 `ko` 정상 선택, 입력 크기 정상. 남은 것은 엔진 자체의 크기 민감도였다.

**실제 캡처 덤프로 확인한 배율 효과** (합성이 아니라 앱이 엔진에 넘긴 그 픽셀):

| 덤프 | 단어높이 | x1 | 조정 후 |
|---|---|---|---|
| 431x109 코드 화면 | 13 | `ID_TOOQOCR_CLIPBOARD`, `toots 테 이 들에 들`, `P% 무선(알따 보존)` | x2: `ID-TOOL-OCR-CLIPBOARD`, `toots 테이블에 등록`, `PNG 우선(알파 보존)` |
| 220x50 | 14 | `찡했어요` | x2: `찜했어요` |
| 700x91 자막체 | 42 | `좋구요! ~` (허깨비 단어) | x0.5: `좋구요!` |

### 4.5.1-a 정답셋 기반 측정 (2026-09-13) — 파라미터를 바꿀 땐 반드시 여기서 재고 바꾼다

`testdata/ocr/` 에 실제 캡처 덤프 36장 + 손으로 옮긴 정답 16장(`groundtruth.txt`) + 평가 하네스가 있다.
**`.gitignore` 로 repo 에는 올리지 않는다** — 화면 캡처라 무엇을 보고 있었는지가 드러난다 (Common §1A).
`bin/` 을 정리하면 원본이 날아가므로 이쪽으로 복사해 둔 것이다.

```
ocr_bench.ps1 <폴더> raw > bench.tsv     # 배율별 인식 결과 수집
eval.py bench.tsv                        # 선택 전략별 정확도
eval_subset.py bench.tsv                 # 배율 후보 개수별 정확도
eval_sim.py bench.tsv                    # 유사도 척도 비교
```

측정 결과 (16장, 글자 단위 정확도 / 완전일치):

| 구성 | 평균 | 완전일치 |
|---|---|---|
| 전처리 없음 + 글자수 최다 (**옛 방식**) | 0.866 | 4/16 |
| 전처리 없음 + 합의 | 0.899 | 6/16 |
| 회색조 + 합의 | 0.904 | 7/16 |
| 자동대비 + 합의 | 0.905 | 7/16 |
| **선명화 + 합의 (현재)** | **0.927** | **8~9/16** |
| 선명화 과다(percent 200) + 합의 | 0.902 | 8/16 |
| oracle (배율을 신이 알려줬을 때) | 0.940 | 9/16 |

**그래서 `run_ocr_on_bgra` 는 이렇게 동작한다.**

1. **언샤프 마스크를 원본에 한 번** 건다 (분리형 5-tap 가우시안, percent 160, threshold 2).
   확대 *후* 거는 쪽은 더 나빴다 (0.927 vs 0.901). 강도를 올리면 오히려 나빠진다.
   **인식은 선명화 버퍼로, 색 추출은 원본 버퍼로** 한다 — 선명화가 가장자리 색을 과장하기 때문이다.
2. 0/90/270 을 각각 인식해 `score_orientation` 이 가장 높은 방향을 고른다 (방향은 결과가 아예 다르므로
   합의를 쓸 수 없다).
3. 그 방향에서 배율 후보 `{1.0, 1.2, 1.4, 1.6, 2.0, 2.5}` 를 전부 인식한 뒤,
   **다른 후보들과 가장 많이 일치하는 결과**(medoid)를 고른다. 유사도는 글자 bigram Dice 계수.
4. 동점이면 더 작은 배율. 확대는 `MaxImageDimension`(10000) 과 총 4천만 픽셀에서 잘라낸다.

**"글자다운 문자 수가 가장 많은 것" 으로 되돌리지 말 것.** 그건 양을 잴 뿐 정확도를 재지 않는다 —
`1054007` 을 정확히 읽은 배율들을 제치고, **2x14 픽셀짜리 쉼표 잡티** 하나가 더 붙어 8자가 된 배율이
채택되어 `, A054007` 이 나왔다. 합의는 "옳게 읽힌 결과는 여러 배율에서 서로 비슷하고, 잘못 읽힌 결과는
제각각" 이라는 성질을 쓴다.

**"검출된 단어 높이의 중앙값으로 배율을 계산" 하는 방식으로도 되돌리지 말 것.** 엔진이 못 찾은 글자는
중앙값에 잡히지 않는다. 큰 제목만 검출된 사진에서 중앙값 44 가 나와 **x0.59 로 줄여** 작은 글자를
뭉갰다. 정답은 x1.3~x2 로 **키우는** 것이었다.

**후보 간격을 넓히지 말 것.** 결과가 배율에 대해 단조롭지 않다 — 같은 사진에서 x1.3 / x2.0 은 제목을
읽었는데 그 사이의 x1.6 과 바로 위 x2.4 는 못 읽었다. 후보 6개에서 9개로 늘려도 +0.003 뿐이라 6개로 둔다.

**측정 방법에 대한 주의:** PIL 로 재고 C++ 로 옮기면 같은 수치가 안 나올 수 있다. 언샤프도 유사도도
**C++ 로 옮길 수 있는 형태(정수 5-tap 커널 / bigram Dice)로 다시 재서** 같은 성능을 확인한 뒤 이식했다.

**진단 로그는 유지한다** — `[OCR]` 태그가 입력 크기·alpha 범위·선택 엔진·정규화 배율·단어별
상자/색/크기·소요시간을 남기고, 엔진에 넘긴 픽셀을 `Log\ocr_input_*.png` 로 덤프한다.
(개발단계 로그 — 커밋 시 메시지에 명시. 최종 테스트 후 일괄 정리.)

**다른 엔진은 추가하지 않는다** (사용자 지시 2026-09-12. Tesseract 는 과거에 시도했고 인식률이 좋지 않았다).

- 전처리는 언샤프 마스크 하나뿐이다 (§4.5.1-a). 이진화는 쓰지 않는다 — Elysium 의 `recognize_cell_rect` 가
  쓰는 6밴드 이진화 + 최빈값 투표는 "작은 흰 숫자 오버레이" 전용이고, 일반 화면 문장에는 해가 된다
  (자막체 실측에서 도움이 되지 않았다).
- 캡처 픽셀은 BitBlt 결과라 alpha 가 0 이다. `PixelFormat32bppRGB` 로 넘겨 alpha 를 무시시킨다 —
  32bppARGB 로 주면 OcrEngine 이 전면 투명으로 읽어 아무것도 인식하지 못한다.
- 인식 결과는 `CSCDeskToolsDlg::show_message_tagged()` 로 화면에 띄웠다가 1초 뒤 fade-out 한다
  (`CSCShapeDlg` + `fade_in(0, 1000, true)` — SCColorTable·nFTDServer 와 같은 패턴).
  **토스트의 소유자는 캡처 노트가 아니라 메인 다이얼로그다.** `CSCShapeDlg` 의 fade 는 detach 된
  thread 라 소유자가 먼저 사라지면 해제된 객체를 건드리는데, 노트는 ESC 한 번에 self-delete 된다.
- `show_message` 는 `<` `>` `&` 를 HTML 엔티티로 escape 한다. `CSCParagraph` 가 `<...>` 를 서식 태그로
  읽으므로, OCR 결과 같은 임의 문자열을 그대로 넘기면 그 조각이 통째로 사라진다.
- 인식기 언어는 `TryCreateFromUserProfileLanguages()` 가 고른다. 이 머신은 `ko`/`ja` 만 설치돼 있고
  `en` 은 없다 (SCWinOcr 의 "en" 폴백은 여기서 무효).

### 4.5.2 캡처 노트의 회전 두 가지 — 섞지 말 것

- **`Alt+←/→`(15도) / `Alt+Shift+←/→`(1도)** — `CSCD2ImageDlg::set_render_angle`. **표시만** 돌린다.
  픽셀 버퍼는 그대로라 복사·저장·OCR 에는 반영되지 않는다. 기존 기능이다.
- **`Ctrl+←/→`** — `rotate_image()`. **이미지 자체**를 90도 돌린다 (`CSCGdiplusBitmap::rotate`).
  `m_bgra_data` / `m_image` / 창 크기를 함께 바꾸므로 복사·저장·OCR 에 모두 반영된다.
  회전 전 표시 배율을 먼저 재 두고 새 비율에 맞춰 client 를 잡아, 돌려도 같은 크기로 보인다.

90도 회전에 `Alt+Shift+←/→` 를 쓰려다 기존 1도 회전과 충돌해 `Ctrl` 로 옮겼다.
Alt 계열 방향키는 이미 자유각 회전이 전부 쓰고 있다.

### 4.6 DPI 정책 — Per-Monitor V2 고정. UI 는 스케일, 화면 픽셀은 스케일 금지 (강제)

매니페스트는 `PerMonitorHighDPIAware` 다 (`29a266a(2026-06-29 17:50:58)`).
**"없음(DPI unaware)" 으로 되돌리지 말 것.** unaware 로 두면 Windows 가 논리 픽셀만 보여줘,
175% 모니터에서 3840 폭 화면이 2194 로 축소·리샘플된 이미지로 들어온다. 캡처·돋보기·줄자가
전부 실제 화면 픽셀을 다루지 못하게 된다 — 이 앱의 존재 이유가 무너진다.

대신 스케일링 책임이 OS 에서 앱으로 넘어왔으므로 **코드에 적는 크기를 직접 환산해야 한다.**

**판정 기준은 "그것이 올라탄 판이 DPI 를 따라 커지는가" 다.** 크롬은 자기가 올라탄 판을 따라간다.

| | 처리 |
|---|---|
| **DPI 로 커지는 판 위의 UI** — 메인 다이얼로그의 여백·버튼·폰트, 오버레이의 핸들·라벨 | `scaled()` / `scaled_f()` 로 환산 |
| **고정 픽셀 판 위의 UI** — 캡처 노트 안의 닫기 버튼·리사이즈 여백·정보 문자열 | **환산 금지.** 상수 그대로 |
| **화면 픽셀 자체** — 캡처 영역 좌표, 줄자가 재는 길이, 돋보기가 확대할 픽셀 수, 노트의 100% 표시, 눈금 간격 | **환산 금지.** 물리 픽셀이 곧 의미 |

**`CSCCapturedNoteDlg` 는 DPI 스케일을 하지 않는 창이다** (`m_dpi` / `scaled()` 자체가 없다).
client 가 이미지 픽셀과 1:1 이라 320px 캡처는 어느 배율에서도 320px 인데, 그 위의 버튼만 DPI 로
커지면 175% 에서 노트 폭의 11.6%, 100% 에서 6.6% 로 비율이 깨진다. 사용자 지적 (2026-09-04) —
*"이미지 크기가 320px로 동일하게 표시되고 있다면 버튼 또한 똑같이 6.6%로 표시되야 하는거 아닌가?"*
DPI 변경 시 이 창이 할 일은 **크기 복원 하나뿐**이다 — OS 가 배율비로 창을 줄이려 하므로.
(처음에 버튼 크기에 상한을 두는 미봉책을 시도했다가 지적받았다 — *"크기를 맞도록 보정해야지
이게 상한을 둔다고 되는 일인가?"*. 보정의 본질은 잘못된 스케일을 빼는 것이지 덧대는 것이 아니다.)

- 헬퍼는 `Common/win_compat/dpi.h` (XP-safe, 미지원 OS 는 96). `CSCFrozenOverlayDlg` / `CSCCapturedNoteDlg` /
  `CSCDeskToolsDlg` / `CSCDropperDlg` 가 각자 `m_dpi` + `scaled()` 를 갖는다.
- 오버레이는 가상 데스크톱 전체를 덮어 "이 창의 모니터" 가 모호하므로 **띄운 시점의 커서 모니터** DPI 로 고정한다.
- **GDI+ 는 `Graphics` 의 DPI 로 pt→px 를 환산**한다. 창 DC(=모니터 DPI)로 측정하고 비트맵(=96)에 그리면
  글자만 작아진다. `Bitmap::SetResolution` 으로 맞출 것. `DrawImage(x,y)` 도 해상도 비율만큼 확대/축소한다.
- **줄자의 cm 환산**은 `MDT_RAW_DPI`(패널 물리 DPI)를 쓴다. 배율이 섞인 effective DPI 가 아니다.
- 아직 안 된 것: `WM_DPICHANGED` 미처리 — 창 생성 시점의 DPI 만 반영된다. 모니터 간 이동 시 재배치 안 됨.

**Why:** `29a266a` 가 매니페스트만 바꾸고 이 전제에 기대던 상수들을 점검하지 않아, 두 달 뒤 175% 에서
메인 창 글자가 버튼을 넘치는 것으로 드러났다. 규칙은 `Common/claude.md §5C.1`.

---

## 5. 미해결 / 다음 작업 후보

집·회사 사이 작업 이어가기 위한 todo 큐. 우선순위·진행 상태는 그때그때 업데이트.

- [ ] 즐겨찾기 툴 ID 목록을 레지스트리에서 load/save (현재 `kDefaultFavorites` 하드코딩)
- [ ] 즐겨찾기 편집 설정 다이얼로그
- [ ] 단축키 설정 다이얼로그 + 레지스트리 저장 (`settings\hotkey\<tool_key>` = `MOD|VK` 패킹). 현재 `kHotkeys` 하드코딩
- [ ] 마지막 캡처 영역 재캡처 (readme 우선순위 1)
- [ ] 딜레이 캡처 (3/5/10s)
- [ ] 크로스헤어 가이드 (frozen overlay 베이스 활용)
- [ ] 창 정보 표시 패널 (HWND/클래스/proc/위치)
- [ ] 노트 위 주석·낙서 (현재 NoteDlg 위에 그릴 수 있는 추가 레이어)
- [ ] **사용설명서에 기능별 스크린샷 추가** (2026-09-08 주 예정). 문서는 아래 §5.1 참조.
      절 구성은 이미 잡혀 있으므로 각 절에 맞는 화면만 캡처해 첨부하면 된다.

### 5.1 사용설명서 (Confluence)

- 위치: 연구개발 스페이스(`2HfRugOJ0HVK`) / 폴더 `47906818` 아래, 페이지 ID **48168961**
  — https://koinodoc.atlassian.net/wiki/spaces/2HfRugOJ0HVK/pages/48168961
- 2026-09-04 작성. 버전 2026.9.4.1 기준. 내용은 전부 소스에서 확인한 것만 기재
  (단축키 표, 캡처 노트 마우스/키보드/우클릭 메뉴, 영역 캡처 Shift 조정 모드,
  스냅 각도 5°/15°/45°, 저장 파일명 규칙, 레지스트리 키).
- **이 문서만 라이브 문서(live doc)다.** 작성 직후 사용자가 방침을 "일반 페이지" 로 바꿨다
  (`Common/claude.md` 의 Confluence 항목). 앞으로 새로 만드는 문서는 페이지로,
  이 문서는 URL 유지를 위해 그대로 둔다.
- 갱신 시 주의: `updateConfluencePage` 는 본문 전체를 다시 써 보내므로 **목차 매크로와
  Change History 매크로를 매번 다시 넣어야** 한다. `contentFormat: "html"` 왕복 필수.

---

## 6. 컨텍스트 동기화 운영

- **집↔회사 어느 머신에서든 작업을 끝내기 전** 이 파일에 *결정 / 선택 이유 / 진행 상태* 를 짧게 추가하고 commit & push. WIP push 무방 (전역 규칙).
- 다른 머신에서 시작 시 `git pull` → 이 파일이 자동 컨텍스트로 로드됨.
- 홈 폴더 auto-memory 는 머신 간 이동 안 됨 — **여기로 이관**.
- 일반적인 코딩 규칙(네이밍·인코딩·MFC 패턴 등) 은 모두 `Common/claude.md` 가 권위. 이 파일은 SCDeskTools 한정 사실만.

---

## 자체 패치 (Self Patch)

2026-09-07 적용. 공통 모듈 `Common/system/CSCSelfPatch/SCSelfPatch.{h,cpp}` 를 쓴다 —
`InitInstance` 앞부분에서 `m_self_patch.startup()`, `ExitInstance` 에서 `m_self_patch.shutdown()`.

- 서버 폴더: `/download/tools/KoinoTools/SCDeskTools` (`ss.linkmemine.com:443`)
- 실행한 그 자리에서 패치한다. 정해진 설치 폴더가 없으므로 exe 를 어디에 두든 바로가기·시작프로그램 등록이 그대로 유효하다.
- 부팅 시 자동 실행 등록 대상이다 (`register_startup = true`).

원리·배포 절차는 <https://koinodoc.atlassian.net/wiki/spaces/2HfRugOJ0HVK/pages/48562184> 참조.
