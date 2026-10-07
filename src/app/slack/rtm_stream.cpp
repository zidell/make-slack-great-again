// RtmPresence's event stream and its log (the fork's; see rtm_presence.h).
#include "app/slack/rtm_presence.h"

#include "base/log.h"
#include "base/str.h"
#include "base/time.h"
#include "net/net.h"
#include "plat/plat.h"

#include <algorithm>
#include <utility>

namespace slack {

namespace {

// The hourly line: what the stream carried and how well the link held.
constexpr int kSummaryMs = 60 * 60'000;

} // namespace

void RtmPresence::startStats() {
    _statsSince = base::monotonicMs();
    _statsTimer = _app.addTimer(kSummaryMs, true, [this] { logSummary(); });
}

// A hello: the stream is up (again: what the gap held was never streamed).
void RtmPresence::streamHello() {
    const int64_t     now = base::monotonicMs();
    const std::string down =
        _downSince ? str::concat({", down ", str::number((now - _downSince) / 1000), " s"})
                   : std::string();
    LOG_INFO(
        "rtm",
        "%s: streaming (hello %lld ms after rtm.connect, attempt %d%s)",
        who(),
        (long long)(_connectAt ? now - _connectAt : -1),
        _attempt,
        down.c_str()
    );
    _attempt   = 0;
    _downSince = 0;
    if (std::exchange(_helloSeen, true) && onResumed) {
        auto fn = onResumed;
        fn();
    }
}

void RtmPresence::streamEvent(std::string_view type, const json::Value &root) {
    // Replies to our own frames aside.
    if (type.empty() || type == "reconnect_url" || root.has("reply_to"))
        return;
    countFrame(type);
    LOG_DEBUG(
        "rtm",
        "%s: %.*s %.*s",
        who(),
        int(type.size()),
        type.data(),
        int(root["channel"].str().size()),
        root["channel"].str().data()
    );
    if (!onEvent)
        return;
    auto fn = onEvent;
    fn(root);
}

int64_t RtmPresence::frameAgeMs() const {
    return _lastFrameMs ? base::monotonicMs() - _lastFrameMs : -1;
}

void RtmPresence::countFrame(std::string_view type) {
    for (auto &[t, n] : _frames)
        if (t == type) {
            ++n;
            return;
        }
    if (_frames.size() < 40) // a bounded table: the rare types share "other"
        _frames.emplace_back(std::string(type), 1);
    else
        countFrame("other");
}

void RtmPresence::logSummary() {
    const int64_t now = base::monotonicMs();
    const int64_t up  = _activeMs + (_activeSince ? now - _activeSince : 0);
    const int64_t all = std::max<int64_t>(1, now - _statsSince);
    if (!holding() && up == 0 && _frames.empty())
        return; // off the whole hour: nothing to say
    std::sort(_frames.begin(), _frames.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });
    std::string types;
    int         total = 0;
    for (const auto &[t, n] : _frames) {
        total += n;
        if (types.size() < 400)
            types += str::concat({types.empty() ? "" : ", ", t, " ", str::number(int64_t(n))});
    }
    LOG_INFO(
        "rtm",
        "%s: last %lld min — streaming %lld%% of the time, %d drops, %d reconnects, %d events (%s)",
        who(),
        (long long)(all / 60000),
        (long long)(up * 100 / all),
        _drops,
        _reconnects,
        total,
        types.c_str()
    );
    _frames.clear();
    _drops = _reconnects = 0;
    _activeMs            = 0;
    _activeSince         = _activeSince ? now : 0;
    _statsSince          = now;
}

bool RtmPresence::delivering() const {
    return _state == Link::Active && connected();
}

void RtmPresence::reconnectNow(const char *why) {
    if (!connected())
        return; // already on its way back (or dropped on purpose)
    LOG_WARN(
        "rtm",
        "%s: %s — re-establishing (last frame %lld ms ago)",
        who(),
        why,
        (long long)frameAgeMs()
    );
    teardown();
    scheduleReconnect();
}

void RtmPresence::wake(const char *why) {
    if (!holding() || _unavailable || _state == Link::Idle)
        return;
    if (connected()) {
        // Open, maybe dead (a sleep, a network switch): a ping must bring a
        // frame back within the probe window, or it is replaced. A live
        // socket is kept (network events come in bursts).
        if (_probeTimer)
            return; // a probe is out already
        LOG_INFO(
            "rtm",
            "%s: %s — probing the open socket (last frame %lld ms ago)",
            who(),
            why,
            (long long)frameAgeMs()
        );
        const int64_t sent = base::monotonicMs();
        _ws->sendText(str::concat({"{\"type\":\"ping\",\"id\":", str::number(++_pingId), "}"}));
        _probeTimer = _app.addTimer(_t.probeMs, false, [this, sent] {
            _probeTimer = 0;
            if (_lastFrameMs >= sent || !connected())
                return; // it answered (or is gone already)
            LOG_WARN(
                "rtm",
                "%s: no frame %d ms after the wake probe — reconnecting at once",
                who(),
                _t.probeMs
            );
            teardown();
            _reconnectMs = _t.reconnectMinMs;
            setState(Link::Connecting);
            ensureHolding();
        });
        return;
    }
    LOG_INFO("rtm", "%s: %s — socket down, reconnecting at once", who(), why);
    teardown(); // its reconnect timer too: no backoff after a wake
    _reconnectMs = _t.reconnectMinMs;
    setState(Link::Connecting);
    ensureHolding();
}

} // namespace slack
