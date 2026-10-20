#include "app/slack/browser_login.h"
#include "app/slack/fork_browser_import.h"
#include "app/model/jobs.h"
#include "base/i18n.h"
#include "base/time.h"
#include "plat/plat.h"

namespace slack {

void BrowserLogin::startRegular(const std::string &exe) {
    _regular      = true;
    _regularExe   = exe;
    _regularAlive = std::make_shared<char>(0);
    _proc         = std::make_shared<base::Process>();
    // Chrome forwards this to its regular profile, including an existing
    // browser process. No guest/incognito/temp profile or debugging endpoint.
    if (!_proc->start(exe, {"--new-tab", "https://slack.com/signin"})) {
        _app.post([this, alive = std::weak_ptr<char>(_regularAlive)] {
            if (!alive.expired())
                finish({}, {}, "launch_failed");
        });
        return;
    }
    _startMs = double(base::monotonicMs());
    setProgress(
        i18n::tr(
            "Sign in to Slack in the browser window \xE2\x80\x94 msga picks "
            "it up automatically."
        )
    );
    _pollTimer = _app.addTimer(2000, true, [this] { pollRegular(); });
    pollRegular();
}

void BrowserLogin::pollRegular() {
    if (_done || _regularReading)
        return;
    if (double(base::monotonicMs()) - _startMs > 5 * 60 * 1000) {
        finish({}, {}, "timeout");
        return;
    }
    if (_proc)
        (void)_proc->running(); // reap the forwarding wrapper, without owning Chrome's lifetime
    _regularReading          = true;
    auto              result = std::make_shared<LocalImport>();
    const std::string exe    = _regularExe;
    model::runInBackground(
        _app,
        [result, exe] { *result = importBrowserSession(exe); },
        [this, result, alive = std::weak_ptr<char>(_regularAlive)] {
            if (alive.expired())
                return;
            _regularReading = false;
            if (result->ok())
                finish(std::move(result->cookie), std::move(result->teams), {});
        }
    );
}

} // namespace slack
