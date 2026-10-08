// Fork: a message posted with blocks whose `text` came back flattened (line
// breaks as spaces) shows the blocks' lines, blank lines included.
#include "app/mrkdwn/markdown.h"
#include "app/slack/slack_json.h"
#include "base/json.h"
#include "support/test.h"

TEST("slack flat text: the blocks' lines replace a flattened text") {
    model::Store   store;
    json::Document d;
    REQUIRE(d.parse(
        std::string_view(R"({"ts": "1.0", "user": "U1", "text": "one  two  • a  • b",
        "blocks": [{"type": "rich_text", "elements": [
          {"type": "rich_text_section", "elements": [{"type": "text", "text": "one\n\ntwo"}]},
          {"type": "rich_text_list", "style": "bullet", "elements": [
            {"type": "rich_text_section", "elements": [{"type": "text", "text": "a"}]},
            {"type": "rich_text_section", "elements": [{"type": "text", "text": "b"}]}]}]}]})"),
        nullptr
    ));
    CHECK_STR(slack::mapjson::toMessage(d.root(), store).text, "one\n\ntwo\n• a\n• b");
    // One line either way: the text stays (its mrkdwn as sent).
    REQUIRE(d.parse(
        std::string_view(R"({"ts": "2.0", "text": "*raw* x", "blocks": [{"type": "rich_text",
        "elements": [{"type": "rich_text_section", "elements": [
          {"type": "text", "text": "raw", "style": {"bold": true}},
          {"type": "text", "text": " x"}]}]}]})"),
        nullptr
    ));
    CHECK_STR(slack::mapjson::toMessage(d.root(), store).text, "*raw* x");
    // A text with its lines stays as is.
    REQUIRE(d.parse(
        std::string_view(R"({"ts": "3.0", "text": "a\nb", "blocks": [{"type": "rich_text",
        "elements": [{"type": "rich_text_section", "elements": [
          {"type": "text", "text": "a\nb!"}]}]}]})"),
        nullptr
    ));
    CHECK_STR(slack::mapjson::toMessage(d.root(), store).text, "a\nb");
}

TEST("slack flat text: blank lines around a list survive the blocks") {
    const mrkdwn::Composed c = mrkdwn::compose("intro\n\n- a\n- b\n\nafter");
    json::Document         d;
    REQUIRE(d.parse(c.blocks, nullptr));
    // As Slack echoes it: the text flattened, so the blocks are read.
    CHECK_STR(slack::mapjson::blocksToMrkdwn(d.root()), "intro\n\n• a\n• b\n\nafter");
}
