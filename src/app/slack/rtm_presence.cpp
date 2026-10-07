// RtmPresence (see rtm_presence.h), over net::WebSocket.
#include "app/slack/rtm_presence.h"

#include "app/slack/web_api.h"

#include "base/log.h"
#include "base/str.h"
#include "base/time.h"
#include "plat/plat.h"

#include <algorithm>
#include <utility>

namespace slack {

namespace {

// Pings unanswered before a silent socket counts as dead (a minute at the
// default cadence; also what a sleep gap looks like).
constexpr int kMaxMissedPongs = 2;

// rtm.connect answers meaning "this token never gets a socket" (dead
// credentials, or the method refused for it): retrying would only churn the
// rate limit. Anything else backs off and retries.
bool fatalConnectError(const std::string &e) {
    return isAuthError(e) || isMethodUnavailable(e);
}

} // namespace

RtmPresence::RtmPresence(plat::App &app, Connect connect, std::string cookie)
    : _app(app), _connect(std::move(connect)), _cookie(std::move(cookie)),
      _alive(std::make_shared<bool>(true)) {
    startStats();
}

RtmPresence::~RtmPresence() {
    *_alive        = false;
    onStateChanged = nullptr;
    teardown();
    stopTimer(_idleTimer);
    stopTimer(_statsTimer);
}

void RtmPresence::stopTimer(uint64_t &id) {
    if (id)
        _app.cancelTimer(id);
    id = 0;
}

bool RtmPresence::connected() const {
    return _ws && _ws->isOpen();
}

void RtmPresence::setMode(Mode mode) {
    if (mode == _mode)
        return;
    static const char *const kModes[] = {"while running", "while using", "native (off)"};
    LOG_INFO("rtm", "%s: presence mode %s", who(), kModes[std::min(int(mode), 2)]);
    _mode        = mode;
    // A deliberate change is a fresh start: forget a refusal (the session may
    // have been re-imported) and any inherited backoff.
    _unavailable = false;
    _reconnectMs = _t.reconnectMinMs;
    if (!holding()) {
        teardown();
        stopTimer(_idleTimer);
        setState(Link::Off);
        return;
    }
    stopTimer(_idleTimer);
    if (_mode == Mode::WhileUsing) // the click that chose it counts as activity
        _idleTimer = _app.addTimer(_t.idleMs, false, [this] {
            _idleTimer = 0;
            onIdle();
        });
    // Idle → WhileRunning brings the link straight back.
    if (_state == Link::Idle || _state == Link::Off)
        setState(Link::Connecting);
    if (connected()) {
        // Already up: only the tickle cadence differs between the modes.
        stopTimer(_tickleTimer);
        if (_mode == Mode::WhileRunning)
            _tickleTimer = _app.addTimer(_t.alwaysTickleMs, true, [this] { sendTickle(true); });
        return;
    }
    ensureHolding();
}

void RtmPresence::noteActivity() {
    if (!holding())
        return;
    if (_mode == Mode::WhileUsing) {
        stopTimer(_idleTimer);
        _idleTimer = _app.addTimer(_t.idleMs, false, [this] {
            _idleTimer = 0;
            onIdle();
        });
        if (_state == Link::Idle) {
            _reconnectMs = _t.reconnectMinMs; // the user is back: no inherited backoff
            setState(Link::Connecting);
            ensureHolding();
            return;
        }
    }
    sendTickle(false);
}

void RtmPresence::ensureHolding() {
    if (!holding() || _unavailable || _connecting || _reconnectTimer)
        return;
    if (_state == Link::Idle || connected())
        return; // dropped on purpose (only noteActivity brings it back), or up
    openAndConnect();
}

void RtmPresence::openAndConnect() {
    if (_connecting)
        return;
    _connecting = true;
    _connectAt  = base::monotonicMs();
    ++_attempt;
    LOG_INFO("rtm", "%s: rtm.connect (attempt %d)", who(), _attempt);
    setState(Link::Connecting);
    // presence_sub keeps Slack from streaming the whole roster's
    // presence_change (we subscribe to nobody); batch_presence_aware is its
    // prerequisite.
    const int gen = _generation;
    _connect(
        "batch_presence_aware=1&presence_sub=true",
        [this, gen, alive = _alive](const json::Document &doc, const std::string &err) {
            if (!*alive || gen != _generation)
                return; // torn down meanwhile: never a competing socket
            if (err.empty()) {
                const std::string_view url = doc.root()["url"].str();
                if (!url.empty()) {
                    connectWs(std::string(url));
                    return;
                }
            }
            _connecting = false;
            if (fatalConnectError(err)) {
                LOG_WARN(
                    "rtm",
                    "%s: rtm.connect refused (%s) — no stream for this workspace, polling only",
                    who(),
                    err.c_str()
                );
                _unavailable = true;
                setState(Link::Unavailable);
                return;
            }
            LOG_INFO("rtm", "%s: rtm.connect failed — %s", who(), err.c_str());
            scheduleReconnect();
        }
    );
}

void RtmPresence::connectWs(const std::string &url) {
    // Never two sockets: each one counts as a client.
    retireSocket(_app, _ws);
    _ws           = std::make_unique<net::WebSocket>(_app);
    _ws->onOpen   = [this] { onOpen(); };
    _ws->onText   = [this](std::string text) { onText(text); };
    _ws->onClosed = [this](int code, std::string reason) { onClosed(code, reason); };
    // The URL carries no auth: the `d` cookie on the handshake IS the auth.
    _ws->open(url, {{"Cookie", "d=" + _cookie}});
}

void RtmPresence::teardown() {
    stopTimer(_probeTimer);
    ++_generation;
    _connecting     = false;
    _connectedSince = 0;
    _awaitingPongs  = 0;
    stopTimer(_reconnectTimer);
    stopTimer(_pingTimer);
    stopTimer(_tickleTimer);
    retireSocket(_app, _ws); // not inside its own callback
}

void RtmPresence::scheduleReconnect() {
    if (!holding() || _unavailable || _reconnectTimer)
        return; // one pending reconnect at a time
    setState(Link::Connecting);
    const int delay = std::max(_reconnectMs, _t.reconnectMinMs);
    ++_reconnects;
    LOG_INFO("rtm", "%s: reconnecting in %d ms", who(), delay);
    _reconnectMs    = std::min(delay * 2, _t.reconnectMaxMs);
    _reconnectTimer = _app.addTimer(delay, false, [this] {
        _reconnectTimer = 0;
        ensureHolding();
    });
}

void RtmPresence::onOpen() {
    _connecting     = false;
    _connectedSince = base::monotonicMs();
    _awaitingPongs  = 0;
    stopTimer(_pingTimer);
    _pingTimer = _app.addTimer(_t.pingMs, true, [this] { sendPing(); });
    // Active comes with `hello`: an unauthenticated handshake opens too.
}

void RtmPresence::onClosed(int code, const std::string &reason) {
    const int64_t now = base::monotonicMs();
    LOG_INFO(
        "rtm",
        "%s: socket closed — code %d %s (up %lld ms, last frame %lld ms ago)",
        who(),
        code,
        reason.c_str(),
        (long long)(_connectedSince ? now - _connectedSince : 0),
        (long long)frameAgeMs()
    );
    _connecting = false;
    stopTimer(_pingTimer);
    stopTimer(_tickleTimer);
    retireSocket(_app, _ws);
    // Only a durable connection resets the backoff (rtm.connect is Tier 1).
    if (_connectedSince && now - _connectedSince >= _t.stableMs)
        _reconnectMs = _t.reconnectMinMs;
    _connectedSince = 0;
    _awaitingPongs  = 0;
    if (holding() && _state != Link::Idle)
        scheduleReconnect();
}

void RtmPresence::onText(const std::string &text) {
    // Every frame is read: besides hello, pong and error the socket carries
    // the workspace's events, which onEvent delivers.
    _lastFrameMs = base::monotonicMs();
    json::Document doc;
    if (!doc.parse(std::string_view(text), nullptr)) {
        LOG_WARN("rtm", "%s: unparsable frame (%zu bytes)", who(), text.size());
        return;
    }
    const std::string_view type = doc.root()["type"].str();
    if (type == "hello") {
        _reconnectMs = _t.reconnectMinMs;
        setState(Link::Active);
        if (_mode == Mode::WhileRunning) {
            stopTimer(_tickleTimer);
            _tickleTimer = _app.addTimer(_t.alwaysTickleMs, true, [this] { sendTickle(true); });
        }
        sendTickle(true); // active at once, not at the first input
        streamHello();
        return;
    }
    if (type == "pong") {
        _awaitingPongs = 0;
        return;
    }
    if (type == "error") {
        // e.g. invalid_auth without the cookie: Slack drops the socket soon
        // anyway; don't wait for it.
        LOG_WARN(
            "rtm",
            "%s: server error frame — %.*s",
            who(),
            int(std::min<size_t>(text.size(), 200)),
            text.data()
        );
        teardown();
        scheduleReconnect();
        return;
    }
    streamEvent(type, doc.root()); // everything else is the event stream
}

void RtmPresence::sendPing() {
    if (!connected())
        return;
    if (_awaitingPongs >= kMaxMissedPongs) {
        LOG_WARN(
            "rtm",
            "%s: no pong for %d pings (last frame %lld ms ago) — reconnecting",
            who(),
            _awaitingPongs,
            (long long)frameAgeMs()
        );
        teardown();
        scheduleReconnect();
        return;
    }
    ++_awaitingPongs;
    _ws->sendText(str::concat({"{\"type\":\"ping\",\"id\":", str::number(++_pingId), "}"}));
}

void RtmPresence::sendTickle(bool force) {
    if (_state != Link::Active || !connected())
        return;
    const int64_t now = base::monotonicMs();
    if (!force && _lastTickle && now - _lastTickle < _t.tickleGapMs)
        return;
    _lastTickle = now;
    _ws->sendText("{\"type\":\"tickle\"}");
}

void RtmPresence::onIdle() {
    if (_mode != Mode::WhileUsing)
        return;
    LOG_INFO("rtm", "%s: no input for %d s — dropping the link", who(), _t.idleMs / 1000);
    teardown();
    setState(Link::Idle);
}

void RtmPresence::setState(Link s) {
    if (s == _state)
        return;
    // The facts the log and the summary need: when streaming started and
    // stopped, and how often it dropped.
    const int64_t now = base::monotonicMs();
    if (_state == Link::Active) {
        _activeMs += _activeSince ? now - _activeSince : 0;
        _activeSince = 0;
        _downSince   = now;
        ++_drops;
        static const char *const kLinks[] = {
            "off", "connecting", "streaming", "idle", "unavailable"
        };
        LOG_INFO("rtm", "%s: stream stopped (now %s)", who(), kLinks[std::min(int(s), 4)]);
    } else if (s == Link::Active) {
        _activeSince = now;
    }
    _state = s;
    if (onStateChanged) {
        auto fn = onStateChanged;
        fn(s);
    }
}

} // namespace slack
