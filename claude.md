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
- 메인 창 즐겨찾기 버튼 (하드코딩)
- **글로벌 단축키** (`RegisterHotKey` 베이스, `kHotkeys` 테이블) — 현재 `Alt+Shift+S` = 창 캡처

### readme 후보 중 미구현
- 마지막 영역 재캡처 / 고정 크기 영역 / 자유 형태 / 스크롤 캡처 / 딜레이 캡처
- 크로스헤어 / 격자 / 가이드 라인
- 색상 팔레트 / contrast / 그라디언트
- 노트 위 주석·낙서 / OCR / 노트 잠금
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

### 4.5 DPI 정책 — Per-Monitor V2 고정. UI 는 스케일, 화면 픽셀은 스케일 금지 (강제)

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

---

## 6. 컨텍스트 동기화 운영

- **집↔회사 어느 머신에서든 작업을 끝내기 전** 이 파일에 *결정 / 선택 이유 / 진행 상태* 를 짧게 추가하고 commit & push. WIP push 무방 (전역 규칙).
- 다른 머신에서 시작 시 `git pull` → 이 파일이 자동 컨텍스트로 로드됨.
- 홈 폴더 auto-memory 는 머신 간 이동 안 됨 — **여기로 이관**.
- 일반적인 코딩 규칙(네이밍·인코딩·MFC 패턴 등) 은 모두 `Common/claude.md` 가 권위. 이 파일은 SCDeskTools 한정 사실만.
