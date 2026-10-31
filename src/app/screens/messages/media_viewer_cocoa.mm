#include "app/screens/messages/media_viewer.h"

#include "app/media/audio_player.h"
#include "app/model/jobs.h"
#include "app/screens/common/downloads.h"
#include "app/screens/common/remote_images.h"
#include "base/file.h"
#include "base/i18n.h"
#include "base/str.h"

#import <AVKit/AVKit.h>
#import <AppKit/AppKit.h>

namespace {
NSString *nativeString(const std::string &s) {
    return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding]
               ?: @"";
}
} // namespace

@interface MSGAMediaWindow : NSWindow
@end
@implementation MSGAMediaWindow
- (BOOL)performKeyEquivalent:(NSEvent *)event {
    const NSEventModifierFlags modifiers =
        event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    if (event.type == NSEventTypeKeyDown &&
        (modifiers & (NSEventModifierFlagCommand | NSEventModifierFlagOption |
                      NSEventModifierFlagControl | NSEventModifierFlagShift)) ==
            NSEventModifierFlagCommand &&
        [event.charactersIgnoringModifiers.lowercaseString isEqualToString:@"w"]) {
        [self performClose:nil];
        return YES;
    }
    return [super performKeyEquivalent:event];
}
- (void)cancelOperation:(id)sender {
    [self close];
}
@end

@interface                                    MSGAMediaViewer : NSObject <NSWindowDelegate>
@property(nonatomic, strong) MSGAMediaWindow *window;
@property(nonatomic, strong) AVPlayerView    *view;
@property(nonatomic, strong) NSTextField     *status;
@property(nonatomic, strong) AVPlayerItem    *item;
@property(nonatomic, copy) NSString          *temporaryPath;
@property(nonatomic) BOOL                     closed;
- (void)loadPath:(const std::string &)path;
@end

@implementation MSGAMediaViewer
- (void)loadPath:(const std::string &)path {
    if (self.closed)
        return;
    self.item = [AVPlayerItem playerItemWithURL:[NSURL fileURLWithPath:nativeString(path)]];
    [self.item addObserver:self
                forKeyPath:@"status"
                   options:NSKeyValueObservingOptionInitial
                   context:nullptr];
    self.view.player = [AVPlayer playerWithPlayerItem:self.item];
    [self.view.player play];
}
- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary *)change
                       context:(void *)context {
    dispatch_async(dispatch_get_main_queue(), ^{
      if (object != self.item || self.closed)
          return;
      if (self.item.status == AVPlayerItemStatusReadyToPlay) {
          self.status.hidden = YES;
      } else if (self.item.status == AVPlayerItemStatusFailed) {
          self.status.stringValue = self.item.error.localizedDescription
                                        ?: nativeString(i18n::tr("Could not read the file."));
          self.status.hidden      = NO;
      }
    });
}
- (void)windowWillClose:(NSNotification *)notification {
    self.closed = YES;
    [self.view.player pause];
    self.view.player = nil;
    if (self.item) {
        [self.item removeObserver:self forKeyPath:@"status"];
        self.item = nil;
    }
    if (self.temporaryPath) {
        screens::removeTempDownload(self.temporaryPath.UTF8String);
        self.temporaryPath = nil;
    }
}
@end

namespace screens {

bool showMediaViewer(Context &ctx, const model::File &f) {
    if (!ctx.window || std::string_view(ctx.app.platform().backendName()) != "cocoa" ||
        (!f.isAudio() && !str::startsWith(f.mime, "video/")))
        return false;
    // One player, including while downloading. A dismissed/replaced download
    // can finish, but must never start playback or leave its temporary copy.
    static MSGAMediaViewer *current = nil;
    [current.window close];
    if (ctx.audio)
        ctx.audio->stop();
    auto *viewer                     = [[MSGAMediaViewer alloc] init];
    current                          = viewer;
    const bool audio                 = f.isAudio();
    viewer.window                    = [[MSGAMediaWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, audio ? 480 : 640, audio ? 150 : 400)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    viewer.window.releasedWhenClosed = NO;
    viewer.window.delegate           = viewer;
    viewer.window.title              = nativeString(f.name);
    viewer.window.minSize            = NSMakeSize(320, audio ? 140 : 220);
    viewer.window.level              = NSFloatingWindowLevel;
    viewer.window.hidesOnDeactivate  = YES;
    viewer.view = [[AVPlayerView alloc] initWithFrame:viewer.window.contentView.bounds];
    viewer.view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    viewer.view.controlsStyle    = AVPlayerViewControlsStyleFloating;
    viewer.window.contentView    = viewer.view;
    viewer.status =
        [NSTextField wrappingLabelWithString:nativeString(i18n::tr("Loading\xE2\x80\xA6"))];
    viewer.status.textColor        = NSColor.whiteColor;
    viewer.status.alignment        = NSTextAlignmentCenter;
    viewer.status.frame            = NSInsetRect(viewer.view.bounds, 24, audio ? 48 : 150);
    viewer.status.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin | NSViewMaxYMargin;
    [viewer.view addSubview:viewer.status];
    NSWindow *parent = (__bridge NSWindow *)ctx.window->native().nativeHandle();
    NSRect    frame  = viewer.window.frame;
    [viewer.window setFrameOrigin:NSMakePoint(
                                      NSMidX(parent.frame) - frame.size.width / 2,
                                      NSMidY(parent.frame) - frame.size.height / 2
                                  )];
    [viewer.window makeKeyAndOrderFront:nil];

    // Voice clips prefer Slack's AAC transcode; videos use the original.
    const std::string source = audio && ctx.audio ? ctx.audio->sourceFor(f) : f.source();
    if (source.empty()) {
        viewer.status.stringValue = nativeString(i18n::tr("Could not read the file."));
        return true;
    }
    if (!RemoteImages::isRemote(source)) {
        [viewer loadPath:str::startsWith(source, "file://") ? file::fromFileUrl(source) : source];
        return true;
    }
    const std::string path =
        tempDownloadPath(ctx.app.platform(), f.name.empty() ? "media" : f.name);
    if (path.empty()) {
        viewer.status.stringValue = nativeString(i18n::tr("Could not read the file."));
        return true;
    }
    const int job = model::jobs().begin(i18n::arg(i18n::tr("Downloading %1"), f.name));
    fetchFile(
        ctx.app.platform(),
        ctx.backend,
        source,
        path,
        [viewer, path, job](bool ok, const std::string &error) {
            model::jobs().end(job);
            if (viewer.closed || !ok) {
                removeTempDownload(path);
                if (!viewer.closed)
                    viewer.status.stringValue =
                        nativeString(error.empty() ? i18n::tr("Could not read the file.") : error);
                return;
            }
            viewer.temporaryPath = nativeString(path);
            [viewer loadPath:path];
        }
    );
    return true;
}
} // namespace screens
