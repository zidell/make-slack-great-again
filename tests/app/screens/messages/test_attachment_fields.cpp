#include "app/screens/messages/attachment_fields.h"
#include "app/slack/slack_json.h"
#include "app/cache/workspace_cache.h"
#include "base/file.h"
#include "support/test.h"
#include "ui/ui.h"

TEST("attachment fields: short pairs keep equal columns and long fields break the row") {
    ui::View parent;
    std::vector<ui::View *> cells;
    screens::buildAttachmentFields(
        &parent,
        {{"Project", "videostew", true}, {"Commit", "abc", true},
         {"Details", "long", false}, {"Committer", "zidell", true},
         {"Branch", "main", true}, {"Odd", "last", true}},
        [&](ui::View *cell, const model::AttachmentField &) {
            cell->add<ui::View>()->style().height(40);
            cells.push_back(cell);
        }
    );
    parent.setFrame({0, 0, 500, 300});
    parent.layout();
    for (size_t i = 0; i < parent.childCount(); ++i)
        parent.child(i)->layout();
    REQUIRE(cells.size() == 6);
    CHECK(cells[0]->frame().w == cells[1]->frame().w);
    CHECK(cells[0]->frame().w > 200);
    CHECK(cells[0]->frame().y == cells[1]->frame().y);
    CHECK(cells[1]->frame().x > cells[0]->frame().x);
    CHECK(cells[2]->frame().w == 500);
    CHECK(cells[3]->frame().w == cells[4]->frame().w);
    CHECK(cells[5]->frame().w == 500);
}

TEST("attachment fields: Slack short survives cache and old fields remain readable") {
    model::Store store;
    json::Document doc;
    REQUIRE(doc.parse(std::string_view(R"({"ts":"1.0","attachments":[{"fields":[
        {"title":"Project","value":"videostew","short":true},
        {"title":"Details","value":"full","short":false},
        {"title":"Legacy","value":"old"}]}]})"), nullptr));
    auto message = slack::mapjson::toMessage(doc.root(), store);
    REQUIRE(message.attachments().size() == 1);
    CHECK(message.attachments()[0].fields[0].shortField);
    CHECK_FALSE(message.attachments()[0].fields[1].shortField);
    CHECK_FALSE(message.attachments()[0].fields[2].shortField);
    std::string error;
    auto app = ui::App::create(&error);
    REQUIRE(app != nullptr);
    const std::string dir = base::test::makeTempDir("attachment-fields-");
    {
        cache::WorkspaceCache wc(app->platform(), store, dir);
        wc.load(nullptr);
        model::Conversation conv;
        conv.id = "C1";
        const auto c = store.addConversation(std::move(conv));
        wc.track(c);
        store.addMessage(c, std::move(message));
        wc.close(true);
    }
    const auto read = [&](bool expected) {
        model::Store loaded;
        cache::WorkspaceCache wc(app->platform(), loaded, dir);
        REQUIRE(wc.load(nullptr));
        const auto c = loaded.findConversation("C1");
        REQUIRE(wc.loadMessages(c) != 0);
        const auto *m = loaded.findMessage(c, model::parseTs("1.0"));
        REQUIRE(m != nullptr);
        CHECK(m->attachments()[0].fields[0].shortField == expected);
        CHECK_STR(m->attachments()[0].fields[1].value, "full");
        wc.close(false);
    };
    read(true);
    const std::string path = file::join(dir, "messages/C1.json");
    std::string bytes;
    REQUIRE(file::readAll(path, &bytes));
    const std::string field = "[\"Project\",\"videostew\",1]";
    const auto pos = bytes.find(field);
    REQUIRE(pos != std::string::npos);
    bytes.replace(pos, field.size(), "[\"Project\",\"videostew\"]");
    REQUIRE(file::writeAtomic(path, bytes));
    read(false);
    CHECK(file::removeTree(dir));
}
