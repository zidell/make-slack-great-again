#include "app/screens/messages/doc_viewer.h"

#include "app/claude/render.h"
#include "app/model/jobs.h"
#include "app/screens/common/downloads.h"
#include "app/screens/common/message_text.h"
#include "app/screens/common/remote_images.h"
#include "app/screens/messages/rich.h"
#include "app/screens/messages/table_view.h"
#include "base/file.h"
#include "base/i18n.h"
#include "base/log.h"
#include "base/str.h"
#include "gfx/icons_generated.h"
#include "ui/controls.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace screens {

using i18n::arg;
using i18n::tr;
using ui::C;

namespace {

constexpr float   kMargin    = 40;  // backdrop visible around the panel
constexpr float   kMaxPanelW = 920; // a readable line, plus the padding
constexpr float   kRadius    = 8;
constexpr int64_t kMaxBytes  = 4 << 20; // bigger: the browser, as before

std::string_view extension(std::string_view name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string_view::npos ? std::string_view() : name.substr(dot + 1);
}

bool isMarkdown(const model::File &f) {
    const std::string_view ext = extension(f.name);
    return str::iequals(f.mime, "text/markdown") || str::iequals(f.mime, "text/x-markdown") ||
           str::iequals(ext, "md") || str::iequals(ext, "markdown");
}

// "## Title ##" → level 2, "Title"; 0 when the line is no heading.
int headingLevel(std::string_view line, std::string *text) {
    const std::string_view t = str::trim(line);
    size_t                 n = 0;
    while (n < t.size() && t[n] == '#')
        ++n;
    if (n == 0 || n > 6 || n >= t.size() || (t[n] != ' ' && t[n] != '\t'))
        return 0;
    std::string_view rest = str::trim(t.substr(n));
    size_t           end  = rest.size();
    while (end > 0 && rest[end - 1] == '#')
        --end;
    if (end < rest.size())
        rest = str::trim(rest.substr(0, end));
    *text = std::string(rest);
    return int(n);
}

// A document's table: when it is cut or squeezed, the whole table opens the
// table viewer (hand cursor over it) instead of the hover pill, which would
// cover the rows the reader is looking at.
class DocTable final : public TableView {
public:
    using TableView::TableView;

    void paintOver(gfx::Painter &) override {}
    bool onEvent(ui::Event &e) override {
        switch (e.type) {
        case ui::EventType::PointerDown:
            return e.button == plat::Button::Left && opens(e.pos);
        case ui::EventType::PointerUp:
            if (opens(e.pos) && onOpenFull) {
                auto fn = onOpenFull;
                fn();
            }
            return true;
        default:
            return false;
        }
    }
    uint8_t cursorAt(ui::PointF local) const override {
        return opens(local) ? uint8_t(plat::Cursor::Hand) : View::cursorAt(local);
    }

private:
    bool opens(ui::PointF local) const {
        return const_cast<DocTable *>(this)->clipped() && bounds().contains(local);
    }
};

class DocViewer final : public ui::Popup {
public:
    DocViewer(Context &ctx, const model::File &f) : _ctx(ctx), _file(f) {
        setCard(false);
        setAnchor({0, 0, 0, 0}, Place::Fill);
        setFocusable(true);
        style().dir = ui::Dir::None;
        _panel      = add<ui::View>();
        _panel->setBackground(C::Surface, kRadius);
        _panel->style().column();
        _panel->setClipChildren(true);
        ui::View *bar = _panel->add<ui::View>();
        bar->style()
            .row()
            .padding(
                ui::metric(ui::M::SpaceL), ui::metric(ui::M::SpaceS), ui::metric(ui::M::SpaceS), 0
            )
            .spacing(ui::metric(ui::M::SpaceXS))
            .items(ui::Align::Center);
        auto *heading = ui::styledLabel(
            bar, f.name, ui::pxFont(13, text::Weight::Bold, ui::themed(C::TextMuted)), 1
        );
        heading->style().flex(1);
        auto *open = bar->add<ui::IconButton>(gfx::Icon::ExternalLink, tr("Open in browser"));
        open->style().size(32, 32);
        open->setIconSize(16);
        open->setVisible(!(f.permalink.empty() && f.path.empty()));
        open->onClick = [this] {
            if (_ctx.openUrl)
                _ctx.openUrl(fileUrl(_file.permalink.empty() ? _file.path : _file.permalink));
        };
        auto *close = bar->add<ui::IconButton>(gfx::Icon::X, tr("Close"));
        close->style().size(32, 32);
        close->setIconSize(16);
        close->onClick = [this] { Popup::close(); };
        _scroll        = _panel->add<ui::ScrollView>();
        _scroll->style().flex(1);
        _scroll->content()->style().padding(40, 16, 40, 40).spacing(4);
        _status = ui::styledLabel(
            _scroll->content(),
            tr("Loading\xE2\x80\xA6"),
            ui::pxFont(15, text::Weight::Regular, ui::themed(C::TextMuted)),
            1
        );
    }

    std::weak_ptr<char> alive() const { return _alive; }

    // The file's text arrived (ok), or why it didn't.
    void show(bool ok, const std::string &text) {
        ui::View *col = _scroll->content();
        if (!ok) {
            _status->setText(text);
            return;
        }
        _status->setVisible(false);
        if (!isMarkdown(_file)) {
            buildBody(_ctx, col, claude::escapeMrkdwn(text), {}, _scroll);
            return;
        }
        for (const DocPart &part : splitMarkdownDoc(text)) {
            if (part.heading) {
                // # 1.6×, ## 1.35×, ### 1.15×, deeper at body size; space above.
                static constexpr float kScale[] = {1.6f, 1.35f, 1.15f, 1, 1, 1};
                auto                  *h        = ui::styledLabel(
                    col,
                    part.text,
                    ui::pxFont(
                        15 * kScale[part.heading - 1], text::Weight::Bold, ui::themed(C::Text)
                    )
                );
                h->style().margins(0, part.heading <= 2 ? 20 : 14, 0, 4);
                continue;
            }
            std::vector<model::Block> blocks = claude::markdownBlocks(part.text);
            if (blocks.empty()) {
                buildBody(_ctx, col, claude::renderMarkdown(part.text), {}, _scroll);
                continue;
            }
            for (model::Block &b : blocks) {
                if (b.kind == model::Block::Kind::Table) {
                    auto *t = col->add<DocTable>(_ctx, std::move(b.rows));
                    t->style().margins(0, 6, 0, 6);
                } else {
                    buildBody(_ctx, col, b.text, {}, _scroll);
                }
            }
        }
    }

    void layout() override {
        const float w = std::min(kMaxPanelW, std::max(200.f, width() - 2 * kMargin));
        const float h = std::max(200.f, height() - 2 * kMargin);
        _panel->setFrame({std::round((width() - w) / 2), std::round((height() - h) / 2), w, h});
    }
    void paint(gfx::Painter &p) override { p.fillRect(bounds(), ui::color(C::ViewerBackdrop)); }
    bool onEvent(ui::Event &e) override {
        if (e.type == ui::EventType::PointerDown) {
            if (e.button == plat::Button::Left && !_panel->frame().contains(e.pos))
                Popup::close();
            return true;
        }
        if (e.type == ui::EventType::KeyDown && e.key == plat::Key::Escape) {
            Popup::close();
            return true;
        }
        return Popup::onEvent(e);
    }

private:
    Context              &_ctx;
    model::File           _file;
    ui::View             *_panel  = nullptr;
    ui::ScrollView       *_scroll = nullptr;
    ui::Label            *_status = nullptr;
    std::shared_ptr<char> _alive  = std::make_shared<char>(0);
};

} // namespace

bool isDocFile(const model::File &f) {
    if (isMarkdown(f))
        return true;
    const std::string_view ext = extension(f.name);
    return str::iequals(ext, "txt") || (str::iequals(f.mime, "text/plain") && ext.empty());
}

std::vector<DocPart> splitMarkdownDoc(std::string_view text) {
    std::vector<DocPart> parts;
    std::string          body;
    const auto           flush = [&] {
        if (!str::trim(body).empty())
            parts.push_back({0, std::string(str::trim(body))});
        body.clear();
    };
    bool inFence = false;
    for (std::string_view line : str::split(text, '\n')) {
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (str::startsWith(str::trim(line), "```"))
            inFence = !inFence;
        std::string heading;
        if (!inFence) {
            if (const int level = headingLevel(line, &heading)) {
                flush();
                parts.push_back({level, std::move(heading)});
                continue;
            }
        }
        body.append(line);
        body += '\n';
    }
    flush();
    return parts;
}

ui::Popup *showDocViewer(Context &ctx, ui::Window &w, const model::File &f) {
    auto                v      = std::make_unique<DocViewer>(ctx, f);
    DocViewer          *raw    = v.get();
    std::weak_ptr<char> alive  = raw->alive();
    const std::string   source = f.source();
    w.showPopup(std::move(v));
    // Read on a worker, shown on the UI thread (if the viewer is still open).
    plat::App &pa   = ctx.app.platform();
    auto       read = [&pa, raw, alive](std::string local, bool temp) {
        auto text = std::make_shared<std::string>();
        auto ok   = std::make_shared<bool>(false);
        model::runInBackground(
            pa,
            [text, ok, local, temp] {
                if (file::size(local) > kMaxBytes)
                    *text = tr("This file is too large to preview.");
                else
                    *ok = file::readAll(local, text.get());
                if (!*ok && text->empty())
                    *text = tr("Could not read the file.");
                if (temp)
                    removeTempDownload(local);
            },
            [raw, alive, text, ok] {
                if (!alive.expired())
                    raw->show(*ok, *text);
            }
        );
    };
    if (source.empty()) {
        raw->show(false, tr("Could not read the file."));
        return raw;
    }
    if (!RemoteImages::isRemote(source)) {
        read(source, false);
        return raw;
    }
    const std::string temp = tempDownloadPath(pa, f.name.empty() ? std::string("file") : f.name);
    if (temp.empty()) {
        raw->show(false, tr("Could not read the file."));
        return raw;
    }
    const int job = model::jobs().begin(arg(tr("Downloading %1"), f.name));
    fetchFile(
        pa,
        ctx.backend,
        source,
        temp,
        [read, temp, job, raw, alive](bool ok, const std::string &err) {
            model::jobs().end(job);
            if (ok) {
                read(temp, true);
                return;
            }
            LOG_WARN("messages", "document preview download failed: %s", err.c_str());
            removeTempDownload(temp);
            if (!alive.expired())
                raw->show(false, arg(tr("Download failed: %1"), err));
        }
    );
    return raw;
}

} // namespace screens
