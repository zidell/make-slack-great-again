#include "app/screens/messages/media_preview.h"
#include "app/slack/slack_json.h"
#include "base/json.h"
#include "support/test.h"

namespace {
model::File mapFile(std::string_view text) {
    json::Document d;
    if (!d.parse(text, nullptr))
        return {};
    model::Store store;
    const auto   m = slack::mapjson::toMessage(d.root(), store);
    return m.files().empty() ? model::File{} : m.files().front();
}
} // namespace

TEST("media previews: video poster keeps the playable and download URLs") {
    const auto f = mapFile(R"({"ts":"1.0","files":[{
        "id":"FV","name":"clip.mp4","mimetype":"video/mp4",
        "url_private":"https://files/clip.mp4",
        "url_private_download":"https://files/original.mp4",
        "thumb_video":"https://files/poster.jpg","thumb_video_w":640,"thumb_video_h":360
    }]})");
    CHECK(screens::hasFilePreview(f));
    CHECK_STR(screens::filePreviewPath(f), "https://files/poster.jpg");
    CHECK_STR(f.path, "https://files/clip.mp4");
    CHECK_STR(f.source(), "https://files/original.mp4");
    CHECK(f.width == 640);
    CHECK(f.height == 360);
}

TEST("media previews: GIF thumbnail leaves the animated original for the viewer") {
    const auto f = mapFile(R"({"ts":"1.0","files":[{
        "id":"FG","name":"animation.gif","mimetype":"image/gif",
        "url_private":"https://files/animation.gif",
        "thumb_720":"https://files/animation.jpg","thumb_720_w":720,"thumb_720_h":480
    }]})");
    CHECK(screens::hasFilePreview(f));
    CHECK(f.isImage());
    CHECK_STR(screens::filePreviewPath(f), "https://files/animation.jpg");
    CHECK_STR(f.path, "https://files/animation.gif");
    CHECK_STR(f.source(), "https://files/animation.gif");
}

TEST("media previews: missing poster keeps a video file chip and GIF original") {
    auto f = mapFile(R"({"ts":"1.0","files":[{
        "name":"clip.mp4","mimetype":"video/mp4","url_private":"https://files/clip.mp4"
    }]})");
    CHECK_FALSE(screens::hasFilePreview(f));
    f.mime   = "image/gif";
    f.path   = "https://files/animation.gif";
    f.width  = 300;
    f.height = 200;
    CHECK(screens::hasFilePreview(f));
    CHECK_STR(screens::filePreviewPath(f), f.path);
    f.mime  = "application/pdf";
    f.thumb = "https://files/page.png";
    CHECK(screens::hasFilePreview(f));
    CHECK_STR(screens::filePreviewPath(f), f.thumb);
}
