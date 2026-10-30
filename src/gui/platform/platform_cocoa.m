/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "platform/platform.h"

struct PlCanvas { int unused; };
static struct PlCanvas s_canvas;

static NSColor *ns(PlColor c)
{
    return [NSColor colorWithSRGBRed:((c >> 16) & 255) / 255.0
                               green:((c >> 8) & 255) / 255.0
                                blue:(c & 255) / 255.0
                               alpha:1.0];
}

static int pl_mods(NSEvent *e)
{
    NSEventModifierFlags f = [e modifierFlags];
    int m = 0;
    if (f & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) m |= PL_MOD_CTRL;
    if (f & NSEventModifierFlagShift) m |= PL_MOD_SHIFT;
    return m;
}

@interface PlView : NSView {
@public
    PlWidgetOps ops;
    void *user;
}
@property(strong) NSTrackingArea *ta;
@end

@implementation PlView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (BOOL)acceptsFirstResponder { return ops.key != NULL; }

- (void)drawRect:(NSRect)dirty
{
    if (ops.paint)
        ops.paint(user, &s_canvas, (int)self.bounds.size.width, (int)self.bounds.size.height);
}

- (void)setFrameSize:(NSSize)s
{
    [super setFrameSize:s];
    if (ops.resize) ops.resize(user, (int)s.width, (int)s.height);
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    if (self.ta) [self removeTrackingArea:self.ta];
    self.ta = [[NSTrackingArea alloc]
        initWithRect:NSZeroRect
             options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                     NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
               owner:self
            userInfo:nil];
    [self addTrackingArea:self.ta];
}

- (void)mouseMoved:(NSEvent *)e
{
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    if (ops.hand_cursor_at) {
        if (ops.hand_cursor_at(user, (int)p.x, (int)p.y)) [[NSCursor pointingHandCursor] set];
        else [[NSCursor arrowCursor] set];
    }
    if (ops.mouse_move) ops.mouse_move(user, (int)p.x, (int)p.y);
}

- (void)mouseDragged:(NSEvent *)e
{
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    if (ops.mouse_move) ops.mouse_move(user, (int)p.x, (int)p.y);
}

- (void)mouseExited:(NSEvent *)e
{
    if (ops.mouse_leave) ops.mouse_leave(user);
}

- (void)mouseDown:(NSEvent *)e
{
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    if (ops.key) [[self window] makeFirstResponder:self];
    if (ops.mouse_down) ops.mouse_down(user, (int)p.x, (int)p.y);
}

- (void)scrollWheel:(NSEvent *)e
{
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    CGFloat d = [e scrollingDeltaY];
    int delta;
    if (!ops.wheel) { [super scrollWheel:e]; return; }
    delta = [e hasPreciseScrollingDeltas] ? (int)(d * 4.0) : (int)(d * 120.0);
    if (delta == 0) return;
    ops.wheel(user, (int)p.x, (int)p.y, delta, pl_mods(e));
}

- (void)keyDown:(NSEvent *)e
{
    int key = PL_KEY_NONE;
    uint32_t ch = 0;
    int mods = pl_mods(e);

    if (!ops.key) { [super keyDown:e]; return; }

    switch ([e keyCode]) {
    case 51:  key = PL_KEY_BACK;  break;
    case 117: key = PL_KEY_DEL;   break;
    case 48:  key = PL_KEY_TAB;   break;
    case 36:
    case 76:  key = PL_KEY_ENTER; break;
    case 53:  key = PL_KEY_ESC;   break;
    case 123: key = PL_KEY_LEFT;  break;
    case 124: key = PL_KEY_RIGHT; break;
    case 126: key = PL_KEY_UP;    break;
    case 125: key = PL_KEY_DOWN;  break;
    case 115: key = PL_KEY_HOME;  break;
    case 119: key = PL_KEY_END;   break;
    case 116: key = PL_KEY_PGUP;  break;
    case 121: key = PL_KEY_PGDN;  break;
    case 99:  key = PL_KEY_F3;    break;
    default:
        {
            NSString *s = (mods & PL_MOD_CTRL) ? [e charactersIgnoringModifiers] : [e characters];
            if ([s length] > 0) {
                unichar u = [s characterAtIndex:0];
                if (u >= 0xD800 && u < 0xDC00 && [s length] > 1) {
                    unichar lo = [s characterAtIndex:1];
                    ch = 0x10000 + (((uint32_t)u - 0xD800) << 10) + ((uint32_t)lo - 0xDC00);
                }
                else ch = u;
                if (ch >= 0x20 && ch != 0x7F && !(ch >= 0xF700 && ch <= 0xF8FF))
                    key = PL_KEY_CHAR;
            }
        }
        break;
    }
    if (key == PL_KEY_NONE) return;
    ops.key(user, key, ch, mods);
}

- (void)dealloc
{
    if (ops.destroy) {
        void (*fn)(void *) = ops.destroy;
        ops.destroy = NULL;
        fn(user);
    }
}
@end

static void place(NSView *v, int x, int y, int w, int h)
{
    NSView *p = [v superview];
    CGFloat fy = y;
    if (p && ![p isFlipped]) fy = p.bounds.size.height - y - h;
    [v setFrame:NSMakeRect(x, fy, w, h)];
}

PlWidget *pl_widget_create(void *parent_native, const PlWidgetOps *ops, void *user,
                           int x, int y, int w, int h)
{
    id obj = (__bridge id)parent_native;
    NSView *parent = [obj isKindOfClass:[NSWindow class]] ? [(NSWindow *)obj contentView]
                                                          : (NSView *)obj;
    PlView *v;
    if (!parent) return NULL;
    v = [[PlView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
    v->ops = *ops;
    v->user = user;
    [parent addSubview:v];            /* the superview owns it */
    place(v, x, y, w, h);
    return (__bridge PlWidget *)v;
}

void pl_widget_destroy(PlWidget *w)
{
    if (w) [(__bridge NSView *)w removeFromSuperview];   /* dealloc runs ops.destroy */
}

void pl_widget_set_rect(PlWidget *w, int x, int y, int width, int height)
{
    place((__bridge NSView *)w, x, y, width, height);
}

int pl_parent_size(PlWidget *w, int *width, int *height)
{
    NSView *p = [(__bridge NSView *)w superview];
    if (!p) return 0;
    *width = (int)p.bounds.size.width;
    *height = (int)p.bounds.size.height;
    return 1;
}

void pl_invalidate(PlWidget *w, const PlRect *r)
{
    NSView *v = (__bridge NSView *)w;
    if (r) [v setNeedsDisplayInRect:NSMakeRect(r->l, r->t, r->r - r->l, r->b - r->t)];
    else   [v setNeedsDisplay:YES];
}


PlFont *pl_font_create(int pt, int bold)
{
    NSFont *f = bold ? [NSFont boldSystemFontOfSize:pt + 3]
                     : [NSFont systemFontOfSize:pt + 3];   /* points -> comparable px size */
    return (PlFont *)CFBridgingRetain(f);
}

PlFont *pl_font_create_family(const char *family, int pt, int bold)
{
    NSFont *f;
    if (!family || !family[0]) return NULL;
    f = [NSFont fontWithName:[NSString stringWithUTF8String:family] size:pt + 3];
    if (!f) {
        f = [[NSFontManager sharedFontManager] fontWithFamily:[NSString stringWithUTF8String:family]
                                                       traits:0 weight:5 size:pt + 3];
    }
    if (!f) return NULL;
    if (bold) f = [[NSFontManager sharedFontManager] convertFont:f toHaveTrait:NSBoldFontMask];
    return (PlFont *)CFBridgingRetain(f);
}

void pl_font_destroy(PlFont *f)
{
    if (f) CFBridgingRelease((CFTypeRef)f);
}

int pl_text_width(PlFont *f, const char *s)
{
    NSString *str = [NSString stringWithUTF8String:s];
    NSDictionary *at = @{ NSFontAttributeName : (__bridge NSFont *)f };
    return (int)ceil([str sizeWithAttributes:at].width);
}

void pl_fill(PlCanvas *c, int l, int t, int r, int b, PlColor col)
{
    [ns(col) setFill];
    NSRectFill(NSMakeRect(l, t, r - l, b - t));
}

void pl_line(PlCanvas *c, int x1, int y1, int x2, int y2, PlColor col)
{
    NSBezierPath *p = [NSBezierPath bezierPath];
    [p moveToPoint:NSMakePoint(x1 + 0.5, y1 + 0.5)];
    [p lineToPoint:NSMakePoint(x2 + 0.5, y2 + 0.5)];
    [p setLineWidth:1.0];
    [ns(col) setStroke];
    [p stroke];
}

void pl_round_rect(PlCanvas *c, PlRect r, PlColor fill, PlColor border, int radius)
{
    NSRect rc = NSMakeRect(r.l, r.t, r.r - r.l, r.b - r.t);
    if (fill != PL_NONE) {
        [ns(fill) setFill];
        [[NSBezierPath bezierPathWithRoundedRect:rc xRadius:radius yRadius:radius] fill];
    }
    if (border != PL_NONE) {
        NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(rc, 0.5, 0.5)
                                                          xRadius:radius yRadius:radius];
        [p setLineWidth:1.0];
        [ns(border) setStroke];
        [p stroke];
    }
}

void pl_ellipse(PlCanvas *c, int l, int t, int r, int b, PlColor fill, PlColor border)
{
    NSRect rc = NSMakeRect(l, t, r - l, b - t);
    if (fill != PL_NONE) {
        [ns(fill) setFill];
        [[NSBezierPath bezierPathWithOvalInRect:rc] fill];
    }
    if (border != PL_NONE) {
        NSBezierPath *p = [NSBezierPath bezierPathWithOvalInRect:NSInsetRect(rc, 0.5, 0.5)];
        [p setLineWidth:1.0];
        [ns(border) setStroke];
        [p stroke];
    }
}

void pl_text(PlCanvas *c, PlFont *f, PlRect r, const char *s, PlColor col, int flags)
{
    NSMutableParagraphStyle *ps = [[NSMutableParagraphStyle alloc] init];
    NSDictionary *at;
    NSString *str = [NSString stringWithUTF8String:s];
    NSSize sz;
    ps.alignment = (flags & PL_TEXT_CENTER) ? NSTextAlignmentCenter : NSTextAlignmentLeft;
    ps.lineBreakMode = (flags & PL_TEXT_ELLIPSIS) ? NSLineBreakByTruncatingTail
                                                  : NSLineBreakByClipping;
    at = @{ NSFontAttributeName : (__bridge NSFont *)f,
            NSForegroundColorAttributeName : ns(col),
            NSParagraphStyleAttributeName : ps };
    sz = [str sizeWithAttributes:at];
    [str drawInRect:NSMakeRect(r.l, r.t + ((r.b - r.t) - sz.height) / 2.0, r.r - r.l, sz.height)
     withAttributes:at];
}

void pl_image(PlCanvas *c, int x, int y, int w, int h, int src_w, int src_h,
              const unsigned char *rgba)
{
    CGContextRef ctx;
    CGColorSpaceRef cs;
    CGDataProviderRef dp;
    CGImageRef img;
    if (!rgba || src_w <= 0 || src_h <= 0 || w <= 0 || h <= 0) return;
    ctx = [[NSGraphicsContext currentContext] CGContext];
    if (!ctx) return;

    cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    dp = CGDataProviderCreateWithData(NULL, rgba, (size_t)src_w * (size_t)src_h * 4, NULL);
    img = CGImageCreate((size_t)src_w, (size_t)src_h, 8, 32, (size_t)src_w * 4, cs,
                        kCGBitmapByteOrderDefault | kCGImageAlphaLast,
                        dp, NULL, true, kCGRenderingIntentDefault);
    if (img) {
        CGContextSaveGState(ctx);
        CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
        CGContextTranslateCTM(ctx, x, y + h);
        CGContextScaleCTM(ctx, 1.0, -1.0);
        CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
        CGContextRestoreGState(ctx);
        CGImageRelease(img);
    }
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
}

@interface PlFlipView : NSView
@end
@implementation PlFlipView
- (BOOL)isFlipped { return YES; }
@end

@interface PlWin : NSObject <NSWindowDelegate> {
@public
    void (*on_size)(void *, int, int);
    void (*on_close)(void *);
    void *user;
}
@property(strong) NSWindow *window;
@property(strong) PlFlipView *content;
@end

@implementation PlWin
- (void)windowDidResize:(NSNotification *)n
{
    NSSize s = [self.content bounds].size;
    if (on_size) on_size(user, (int)s.width, (int)s.height);
}

- (BOOL)windowShouldClose:(id)sender
{
    if (on_close) { on_close(user); return NO; }
    return YES;
}

- (void)windowWillClose:(NSNotification *)n
{
    if ([NSApp modalWindow] == self.window) [NSApp stopModal];
}
@end

PlWindow *pl_window_create(void *owner_native, const char *title, int w, int h, int min_w, int min_h,
                           void (*on_size)(void *u, int w, int h),
                           void (*on_close)(void *u), void *user)
{
    PlWin *pw = [[PlWin alloc] init];
    NSWindow *win;
    NSWindow *owner = nil;
    id o = (__bridge id)owner_native;
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskResizable;

    if ([o isKindOfClass:[NSWindow class]]) owner = (NSWindow *)o;
    else if ([o isKindOfClass:[NSView class]]) owner = [(NSView *)o window];

    win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, w, h)
                                      styleMask:style
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
    [win setReleasedWhenClosed:NO];
    [win setTitle:title ? [NSString stringWithUTF8String:title] : @""];
    [win setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
    if (min_w > 0 && min_h > 0) [win setContentMinSize:NSMakeSize(min_w, min_h)];

    pw.content = [[PlFlipView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
    [win setContentView:pw.content];
    pw.window = win;
    pw->on_size = on_size;
    pw->on_close = on_close;
    pw->user = user;
    [win setDelegate:pw];

    if (owner) {
        NSRect of = [owner frame];
        NSRect wf = [win frame];
        [win setFrameOrigin:NSMakePoint(NSMidX(of) - wf.size.width / 2.0,
                                        NSMidY(of) - wf.size.height / 2.0)];
    }
    else [win center];

    return (PlWindow *)CFBridgingRetain(pw);
}

void *pl_window_client(PlWindow *win)
{
    PlWin *pw = (__bridge PlWin *)win;
    return (__bridge void *)pw.content;
}

void pl_window_run_modal(PlWindow *win)
{
    PlWin *pw = (__bridge PlWin *)win;
    [pw.window makeKeyAndOrderFront:nil];
    if (pw->on_size) {
        NSSize s = [pw.content bounds].size;
        pw->on_size(pw->user, (int)s.width, (int)s.height);
    }
    [NSApp runModalForWindow:pw.window];
    [pw.window orderOut:nil];
}

void pl_window_close(PlWindow *win)
{
    PlWin *pw = (__bridge PlWin *)win;
    if ([NSApp modalWindow] == pw.window) [NSApp stopModal];
    [pw.window orderOut:nil];
}

void pl_window_destroy(PlWindow *win)
{
    PlWin *pw;
    if (!win) return;
    pw = (__bridge PlWin *)win;
    pw->on_size = NULL;
    pw->on_close = NULL;
    [pw.window setDelegate:nil];
    [pw.window orderOut:nil];
    [pw.window close];
    CFBridgingRelease((CFTypeRef)win);
}
#endif /* __APPLE__ */