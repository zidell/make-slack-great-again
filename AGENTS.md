# msga 로컬 작업 지침

- Windows에서는 `scripts/build.ps1`로 빌드하고 `build/msga.exe`를 실행한다. 실행 중인 앱을 정상 종료한 뒤 재빌드·재실행하고, 프로세스의 실행 경로를 확인한다.
- Windows 로컬 수정본은 `%APPDATA%\msga\MSGA\settings.json`의 `autoUpdates`를 `false`로 유지한다. 자동 업데이트는 실행 중인 `build/msga.exe`를 공식 배포본으로 교체하므로 수정 사항이 사라진다. 이미 교체됐다면 앱을 정상 종료하고 해당 실행 파일만 제거한 뒤 재빌드해 강제로 다시 링크한다. 빌드·실행 후 자동 업데이트 확인 시점(시작 후 5초)을 지나서도 실행 파일의 해시가 유지되는지 확인한다.

- 이 체크아웃은 사용자가 실제로 쓰는 msga 빌드본이다. 소스를 수정하면 매번 끝에 재빌드하고 빌드본을 다시 실행한다.
  1. `./scripts/build.sh` (테스트를 고쳤거나 추가했으면 `--test`)
  2. 실행 중인 msga를 정상 종료: `osascript -e 'quit app id "com.nisdos.msga"'` (단일 인스턴스라 기존 프로세스가 있으면 새로 뜨지 않는다. `/Applications/msga.app`도 같은 번들 ID다)
  3. `open build/msga.app`으로 실행하고 `pgrep -fl 'build/msga.app'`으로 떠 있는지 확인한다
- `/Applications/msga.app`은 `build/msga.app`을 가리키는 심볼릭 링크다(Dock·Spotlight로 열어도 빌드본이 뜬다). 공식 DMG로 덮어쓰지 않는다.
- 이 Mac은 시스템 언어가 한국어라 `settings_tests`의 로캘 의존 검사 2건(AI 언어 "English", `dateLanguage() == "en"`)이 원본 코드에서도 실패한다. 이 2건 외의 실패만 회귀로 본다.
- macOS 서명: `credentials.cmake`(gitignored)의 `MSGA_CODESIGN_IDENTITY`에 Developer ID 인증서를 지정해 둔다. ad-hoc(`-`)으로 되돌리면 서명 요건이 빌드마다 바뀌는 cdhash가 되어 키체인이 빌드할 때마다 다시 묻는다.
- 원격: `origin` = 포크(`zidell/make-slack-great-again`), `upstream` = 원본(`punarinta/make-slack-great-again`). 작업은 `master`에 커밋해 `origin`에 push한다.
- 원본 업데이트 반영: `git fetch upstream` → `git rebase upstream/master`(충돌은 우리 기능을 살려 해결) → `python3 src/tools/i18n.py update`로 번역 테이블 재생성(바뀌었으면 포크 맨 끝의 i18n 커밋에 `--fixup`) → `./scripts/build.sh --test`로 확인 → `git push --force-with-lease origin master` → 재빌드·재실행. 원본의 `version.cmake`가 올라가면 빌드본 버전도 따라 올라가 업데이트 알림이 사라진다. 원본 배포(버전 증가)마다 바로 반영해 한 번에 쌓이는 충돌을 작게 유지한다.
- 원본 반영 충돌을 줄이는 구조(원본을 아예 안 건드릴 수는 없으니, 건드리는 면적을 줄인다):
  - 포크 커밋은 기능당 하나다. 기존 기능을 고치면 새 커밋을 쌓지 말고 `git commit --fixup=<그 기능 커밋>` 후 `GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash upstream/master`로 합친다(rebase 때 영역마다 충돌을 한 번만 푼다).
  - **포크 기능은 원본 코드를 고쳐서 만들지 않고, 우리 쪽 파일에서 원본에 끼워 넣는(오버라이드) 방식으로 만든다.** 새 기능을 설계할 때 먼저 "원본을 몇 줄 건드리는가"를 기준으로 방식을 고른다.
    - 로직·데이터·UI는 새 파일(예: `screens/shell/channel_tint.*`, `sidebar_order_impl.h`)에 두고, 원본 파일에는 그 파일을 부르는 한두 줄(멤버 하나, 호출 하나, 테이블 항목 하나)만 남긴다. 새 파일은 원본 업데이트와 충돌하지 않는다.
    - 원본 동작을 바꿔야 하면 원본 함수 본문을 고치지 말고, 원본에 범용 훅(함수 포인터·콜백·빈 필드)을 하나 열고 동작은 우리 파일에서 채운다(예: 테마 토큰 덮어쓰기 `ui::setColorOverride`, 메뉴 항목 색 견본 `MenuItem::swatch`, 사이드바 이름 스타일 `Sidebar::styleName`).
    - 원본 함수의 결과를 바꿀 때는 그 함수 대신 바깥 호출 지점에서 덧붙인다(예: 채널 글자색 서브메뉴는 `Menus::chatItems`가 아니라 메뉴를 띄우는 `showChat`에서 `ChannelTints::withMenu`로 끼운다). 그러면 원본 함수와 원본 테스트가 그대로 남는다.
    - 원본 테스트는 고치지 않는다. 기존 항목의 순서·인덱스·키보드 경로가 바뀌지 않게 배치하고, 포크 기능의 테스트는 새 테스트 파일(`tests/.../test_<기능>.cpp`)에 둔다.
  - 원본 동작을 전제로 하는 기능(예: RTM 소켓 프레임을 이벤트로 쓰는 수신)은 테스트로 고정해, 원본이 전제를 바꾸면 rebase 직후 테스트가 깨지게 한다.
  - 레포 설정(클론마다 한 번): `git config rerere.enabled true; git config rerere.autoupdate true; git config merge.ours.driver true`. rerere는 한 번 푼 충돌을 다음 rebase에서 그대로 다시 적용하고, `merge.ours`는 `.gitattributes`가 지정한 번역 테이블(`languages_generated.cpp`)을 손으로 풀지 않고 원본 쪽으로 둔 뒤 재생성하게 한다. `.po`는 `merge=union`이라 양쪽 항목이 모두 남는다(`i18n.py update`가 msgid 기준으로 정리).
- 실행 중인 빌드본은 사용자의 실제 Slack 계정에 붙어 있다. System Events 등으로 키 입력을 보내는 테스트를 하지 않는다(Enter 한 번이 입력창 내용을 실제 채널에 전송한 적이 있다). 동작 확인은 단위 테스트와 사용자 직접 확인으로 한다.
- `claude_backend` 테스트는 전체 실행 중 가끔 단독으로 실패하고 재실행하면 통과한다(타이밍성). 재실행으로 통과하면 회귀로 보지 않는다.
- 진단 로그: `~/Library/Application Support/msga/MSGA/logs/msga-YYYY-MM-DD.log`(하루 한 파일, 14일 보관, 하루 32MB 상한). 메시지 누락·지연·연결 문제를 볼 때는 먼저 이 파일에서 다음을 찾는다.
  - `rtm:` 워크스페이스별 RTM 수명주기(연결 시도·streaming·socket closed·no pong·reconnecting in·wake probe)와 1시간 요약(스트리밍 비율, 끊김·재연결 수, 이벤트 종류별 개수)
  - `slack: … delivery` 실시간/폴링 전환 시점, `… never sent` / `… ahead of the stream` 안전망 폴링이 스트림 누락을 잡은 기록(채널 id, 몇 개, 몇 초 늦었는지)
  - `app: woke from sleep` / `network online|offline` 슬립·네트워크 이벤트
  - 이벤트 하나하나까지 보려면 `MSGA_LOG=debug`로 실행한다(`MSGA_LOG=debug build/msga.app/Contents/MacOS/msga`).
