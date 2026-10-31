#include "app/fake/fake_backend.h"
#include "app/model/jobs.h"
#include "app/screens/common/downloads.h"
#include "app/screens/messages/image_cache.h"
#include "app/screens/messages/media_viewer.h"
#include "base/file.h"
#include "plat/audio.h"
#include "support/test.h"

#import <AVKit/AVKit.h>
#import <AppKit/AppKit.h>

namespace {
struct Backend : fake::FakeBackend {
    using FakeBackend::FakeBackend;
    struct Download {
        std::string path;
        Done        done;
    };
    std::vector<Download> pending;
    void                  downloadFile(const std::string &, std::string path, Done done) override {
        pending.push_back({std::move(path), std::move(done)});
    }
};

NSWindow *playerWindow() {
    for (NSWindow *w in NSApp.windows)
        if ([w isKindOfClass:NSClassFromString(@"MSGAMediaWindow")] && w.visible)
            return w;
    return nil;
}

NSEvent *closeKey(NSWindow *window, NSEventModifierFlags extra = 0) {
    return [NSEvent keyEventWithType:NSEventTypeKeyDown
                            location:NSZeroPoint
                       modifierFlags:NSEventModifierFlagCommand | extra
                           timestamp:0
                        windowNumber:window.windowNumber
                             context:nil
                          characters:@"w"
         charactersIgnoringModifiers:@"w"
                           isARepeat:NO
                             keyCode:13];
}

NSView *statusView(NSWindow *window, NSString *identifier) {
    for (NSView *v in window.contentView.subviews)
        if ([v.identifier isEqualToString:identifier])
            return v;
    return nil;
}
} // namespace

TEST("media viewer: closed and replaced downloads never play and remove their copies") {
    base::test::setEnv("PLAT_BACKEND", "cocoa");
    std::string error;
    auto        app = ui::App::create(&error);
    REQUIRE(app != nullptr);
    model::Store        store;
    Backend             backend(store, app->platform());
    screens::ImageCache images(app->platform());
    screens::Context    ctx{*app, store, backend, images};
    ui::Window          parent(plat::WindowDesc{});
    ctx.window = &parent;
    model::File f;
    f.name                = "video.mp4";
    f.mime                = "video/mp4";
    f.path                = "https://files.test/video.mp4";
    const auto jobsBefore = model::jobs().count();

    REQUIRE(screens::showMediaViewer(ctx, f));
    REQUIRE(backend.pending.size() == 1);
    NSWindow *first = playerWindow();
    REQUIRE(first != nil);
    NSTextField         *downloadStatus = (NSTextField *)statusView(first, @"media-status");
    NSProgressIndicator *downloadProgress =
        (NSProgressIndicator *)statusView(first, @"media-progress");
    REQUIRE(downloadStatus != nil);
    REQUIRE(downloadProgress != nil);
    CHECK([downloadStatus.stringValue isEqualToString:@"Downloading video.mp4"]);
    CHECK_FALSE(downloadProgress.hidden);
    CHECK(downloadProgress.indeterminate);
    CHECK_FALSE([first performKeyEquivalent:closeKey(first, NSEventModifierFlagControl)]);
    CHECK(first.visible);
    [NSApp sendEvent:closeKey(first)];
    CHECK_FALSE(first.visible);
    auto &one = backend.pending[0];
    REQUIRE(file::writeAtomic(one.path, "invalid media"));
    one.done(true, {});
    CHECK_FALSE(file::exists(one.path));
    CHECK_FALSE(file::exists(file::dirName(one.path)));
    CHECK(((AVPlayerView *)first.contentView).player == nil);
    CHECK(model::jobs().count() == jobsBefore);

    REQUIRE(screens::showMediaViewer(ctx, f));
    NSWindow *second = playerWindow();
    f.name           = "audio.m4a";
    f.mime           = "audio/mp4";
    REQUIRE(screens::showMediaViewer(ctx, f));
    NSWindow *third = playerWindow();
    REQUIRE(third != nil);
    CHECK(third != second);
    CHECK_FALSE(second.visible);
    REQUIRE(backend.pending.size() == 3);
    auto &two = backend.pending[1];
    REQUIRE(file::writeAtomic(two.path, "invalid media"));
    two.done(true, {});
    CHECK_FALSE(file::exists(two.path));
    CHECK(((AVPlayerView *)second.contentView).player == nil);
    CHECK(((AVPlayerView *)third.contentView).player == nil);

    // A download failure stays in the viewer, without creating a player.
    auto &three = backend.pending[2];
    three.done(false, "download failed");
    CHECK([((NSTextField *)statusView(third, @"media-status")).stringValue
        isEqualToString:@"download failed"]);
    CHECK(statusView(third, @"media-progress").hidden);
    CHECK_FALSE(file::exists(file::dirName(three.path)));
    CHECK(((AVPlayerView *)third.contentView).player == nil);
    CHECK(model::jobs().count() == jobsBefore);
    [third close];

    // A local silent WAV exercises the real decoder and player lifecycle.
    const std::string local = screens::tempDownloadPath(app->platform(), "silence.wav");
    REQUIRE(!local.empty());
    REQUIRE(
        file::writeAtomic(local, plat::audio::wavFromPcm16(std::string(32000, '\0'), 16000, 1))
    );
    f.name = "silence.wav";
    f.mime = "audio/wav";
    f.path = local;
    REQUIRE(screens::showMediaViewer(ctx, f));
    NSWindow *playing = playerWindow();
    REQUIRE(playing != nil);
    AVPlayerView *view = (AVPlayerView *)playing.contentView;
    REQUIRE(view.player != nil);
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:5];
    while (view.player.currentItem.status == AVPlayerItemStatusUnknown &&
           deadline.timeIntervalSinceNow > 0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    CHECK(view.player.currentItem.status == AVPlayerItemStatusReadyToPlay);
    // The observer posts the transition to the UI queue.
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    CHECK(statusView(playing, @"media-status").hidden);
    CHECK(statusView(playing, @"media-progress").hidden);
    CHECK([playing performKeyEquivalent:closeKey(playing)]);
    CHECK_FALSE(playing.visible);
    CHECK(view.player == nil);
    CHECK(file::exists(local)); // closing must only remove downloaded copies
    screens::removeTempDownload(local);

    f.mime = "image/png";
    CHECK_FALSE(screens::showMediaViewer(ctx, f));
    f.mime     = "audio/mpeg";
    ctx.window = nullptr;
    CHECK_FALSE(screens::showMediaViewer(ctx, f));
}
