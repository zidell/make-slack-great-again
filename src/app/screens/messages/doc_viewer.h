// The document viewer: a Markdown or plain-text file shared in a message
// (.md, .markdown, .txt) read in place over the window, like the image
// viewer, instead of opening its Slack page in the browser. The file is
// fetched with the workspace's credentials to a temporary copy and shown in
// a panel on the viewer backdrop under a small bar (its name, Open in
// browser, Close): Markdown rendered (headings sized by level, tables as
// tables, the rest as message text), plain text as it is. Escape, Close or a
// click on the backdrop dismisses it.
#pragma once

#include "app/model/types.h"
#include "app/screens/messages/context_fwd.h"

#include <string>
#include <string_view>
#include <vector>

namespace ui {
class Popup;
class Window;
} // namespace ui

namespace screens {

// Read in the viewer: Markdown or plain text, by its type or extension.
bool isDocFile(const model::File &f);

// `text` (a whole Markdown document) as the viewer's parts, in order: a
// heading (level 1–6) or a mrkdwn body; tables are bodies' Table blocks.
struct DocPart {
    int         heading = 0; // 0: a body
    std::string text;        // the heading's text, or the body's Markdown
};
std::vector<DocPart> splitMarkdownDoc(std::string_view text);

ui::Popup *showDocViewer(Context &ctx, ui::Window &w, const model::File &f);

} // namespace screens
