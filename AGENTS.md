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
- 원본 업데이트 반영: `git fetch upstream` → `git rebase upstream/master`(충돌은 우리 기능을 살려 해결) → `./scripts/build.sh --test`로 확인 → `git push --force-with-lease origin master` → 재빌드·재실행. 원본의 `version.cmake`가 올라가면 빌드본 버전도 따라 올라가 업데이트 알림이 사라진다.
- 실행 중인 빌드본은 사용자의 실제 Slack 계정에 붙어 있다. System Events 등으로 키 입력을 보내는 테스트를 하지 않는다(Enter 한 번이 입력창 내용을 실제 채널에 전송한 적이 있다). 동작 확인은 단위 테스트와 사용자 직접 확인으로 한다.
- `claude_backend` 테스트는 전체 실행 중 가끔 단독으로 실패하고 재실행하면 통과한다(타이밍성). 재실행으로 통과하면 회귀로 보지 않는다.
