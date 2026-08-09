/**
 * Interactive TerminalC plot window — macOS Cocoa.
 *
 * Controls:
 *   Scroll wheel    zoom toward the cursor
 *   Left-drag       pan
 *   + / =           zoom in
 *   - / _           zoom out
 *   0               reset view
 *   y               auto-fit Y to visible samples
 *   a               print the current view range
 *   Esc / q         close the window
 *   Hover curve     show a readout near the curve
 */
#import <Cocoa/Cocoa.h>

#include "plot_gui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TERMINALC_PLOT_SAMPLES_MIN = 1600,
    TERMINALC_PLOT_SAMPLES_MAX = 8000,
    TERMINALC_PLOT_MARGIN_L = 72,
    TERMINALC_PLOT_MARGIN_R = 24,
    TERMINALC_PLOT_MARGIN_T = 54,
    TERMINALC_PLOT_MARGIN_B = 62,
    TERMINALC_PLOT_HOVER_PX = 14
};

typedef struct {
    TerminalCCalc *calc;
    char expr[TERMINALC_LINE_MAX];
    double x0, x1, y0, y1;
    double ix0, ix1;
    bool auto_y;
    bool dragging;
    NSPoint last_point;
    bool hover_on;
    double hover_x, hover_y;
    NSPoint hover_point;
} TerminalCPlotState;

static NSRect plot_bounds(NSRect bounds)
{
    CGFloat w = bounds.size.width - TERMINALC_PLOT_MARGIN_L - TERMINALC_PLOT_MARGIN_R;
    CGFloat h = bounds.size.height - TERMINALC_PLOT_MARGIN_T - TERMINALC_PLOT_MARGIN_B;
    if (w < 1.0)
        w = 1.0;
    if (h < 1.0)
        h = 1.0;
    return NSMakeRect(TERMINALC_PLOT_MARGIN_L, TERMINALC_PLOT_MARGIN_B, w, h);
}

static NSPoint world_to_screen(const TerminalCPlotState *s, NSRect plot,
                               double x, double y)
{
    double dx = s->x1 - s->x0;
    double dy = s->y1 - s->y0;
    if (dx == 0.0)
        dx = 1.0;
    if (dy == 0.0)
        dy = 1.0;
    return NSMakePoint(plot.origin.x + (CGFloat)((x - s->x0) / dx * plot.size.width),
                       plot.origin.y + (CGFloat)((y - s->y0) / dy * plot.size.height));
}

static void screen_to_world(const TerminalCPlotState *s, NSRect plot, NSPoint point,
                            double *x_out, double *y_out)
{
    double dx = s->x1 - s->x0;
    double dy = s->y1 - s->y0;
    *x_out = s->x0 + (double)(point.x - plot.origin.x) / plot.size.width * dx;
    *y_out = s->y0 + (double)(point.y - plot.origin.y) / plot.size.height * dy;
}

static bool sample_y(TerminalCPlotState *s, double x, double *y_out)
{
    s->calc->x.type = TERMINALC_REAL;
    mpfr_set_d(s->calc->x.r, x, MPFR_RNDN);
    TerminalCValue value;
    if (!terminalc_eval(s->calc, s->expr, &value))
        return false;
    mpfr_t real;
    mpfr_init2(real, s->calc->bits);
    terminalc_as_real(s->calc, real, &value);
    *y_out = mpfr_get_d(real, MPFR_RNDN);
    mpfr_clear(real);
    terminalc_value_clear(&value);
    return isfinite(*y_out);
}

static void auto_fit_y(TerminalCPlotState *s)
{
    double mn = 0.0;
    double mx = 0.0;
    int valid = 0;
    for (int i = 0; i < TERMINALC_PLOT_SAMPLES_MIN; i++) {
        double t = (double)i / (double)(TERMINALC_PLOT_SAMPLES_MIN - 1);
        double x = s->x0 + t * (s->x1 - s->x0);
        double y;
        if (!sample_y(s, x, &y))
            continue;
        if (valid == 0 || y < mn)
            mn = y;
        if (valid == 0 || y > mx)
            mx = y;
        valid++;
    }
    if (valid == 0) {
        s->y0 = -1.0;
        s->y1 = 1.0;
        return;
    }

    double span = mx - mn;
    if (span == 0.0 || !isfinite(span)) {
        double pad = fabs(mx) > 1.0 ? fabs(mx) * 0.1 : 1.0;
        mn -= pad;
        mx += pad;
    } else {
        double pad = span * 0.08;
        mn -= pad;
        mx += pad;
    }
    span = mx - mn;
    if (mn > 0.0 && mn < span * 0.25)
        mn = 0.0;
    if (mx < 0.0 && -mx < span * 0.25)
        mx = 0.0;
    s->y0 = mn;
    s->y1 = mx;
}

static void zoom_about(TerminalCPlotState *s, double wx, double wy, double factor)
{
    if (!(factor > 0.0) || !isfinite(factor))
        return;
    s->x0 = wx - (wx - s->x0) / factor;
    s->x1 = wx + (s->x1 - wx) / factor;
    s->y0 = wy - (wy - s->y0) / factor;
    s->y1 = wy + (s->y1 - wy) / factor;
    if (!(s->x1 > s->x0) || !isfinite(s->x0) || !isfinite(s->x1)) {
        s->x0 = s->ix0;
        s->x1 = s->ix1;
    }
    if (!(s->y1 > s->y0) || !isfinite(s->y0) || !isfinite(s->y1)) {
        s->auto_y = true;
        auto_fit_y(s);
    }
}

static double nice_step(double lo, double hi, int target)
{
    double span = hi - lo;
    if (!(span > 0.0) || !isfinite(span))
        return 1.0;
    if (target < 2)
        target = 2;
    double rough = span / (double)target;
    double exponent = floor(log10(rough));
    double base = pow(10.0, exponent);
    double fraction = rough / base;
    double nice = fraction <= 1.0 ? 1.0 : fraction <= 2.0 ? 2.0
                           : fraction <= 2.5 ? 2.5 : fraction <= 5.0 ? 5.0 : 10.0;
    return nice * base;
}

static void format_tick(char *buffer, size_t size, double value, double step)
{
    if (!isfinite(value)) {
        snprintf(buffer, size, "nan");
        return;
    }
    if (fabs(value) < fabs(step) * 1e-9)
        value = 0.0;
    double av = fabs(value);
    double as = fabs(step);
    if (av != 0.0 && (av >= 1e6 || av < 1e-4)) {
        snprintf(buffer, size, "%.2e", value);
        return;
    }
    int decimals = 0;
    if (as < 1.0) {
        decimals = (int)ceil(-log10(as));
        if (decimals < 0)
            decimals = 0;
        if (decimals > 12)
            decimals = 12;
        if (as < 1e-3)
            decimals++;
    }
    if (decimals > 14)
        decimals = 14;
    snprintf(buffer, size, "%.*f", decimals, value);
    char *dot = strchr(buffer, '.');
    if (dot) {
        char *end = buffer + strlen(buffer) - 1;
        while (end > dot && *end == '0')
            *end-- = '\0';
        if (end == dot)
            *end = '\0';
    }
}

static NSString *string_from_c(const char *text)
{
    NSString *result = [NSString stringWithUTF8String:text];
    return result ? result : @"";
}

static void draw_text(NSString *text, NSPoint point, NSColor *color, NSFont *font)
{
    [text drawAtPoint:point withAttributes:@{
        NSFontAttributeName : font,
        NSForegroundColorAttributeName : color
    }];
}

@interface TerminalCPlotView : NSView {
    TerminalCPlotState _state;
    NSTrackingRectTag _tracking_tag;
}
- (instancetype)initWithFrame:(NSRect)frame state:(const TerminalCPlotState *)state;
@end

@implementation TerminalCPlotView

- (instancetype)initWithFrame:(NSRect)frame state:(const TerminalCPlotState *)state
{
    self = [super initWithFrame:frame];
    if (self) {
        _state = *state;
        _tracking_tag = 0;
        [self updateTrackingAreas];
    }
    return self;
}

- (BOOL)isFlipped
{
    return NO;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)updateTrackingAreas
{
    if (_tracking_tag != 0)
        [self removeTrackingRect:_tracking_tag];
    [super updateTrackingAreas];
    _tracking_tag = [self addTrackingRect:self.bounds owner:self userData:NULL assumeInside:NO];
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    NSRect bounds = self.bounds;
    NSRect plot = plot_bounds(bounds);
    if (_state.auto_y)
        auto_fit_y(&_state);

    [[NSColor windowBackgroundColor] setFill];
    NSRectFill(bounds);

    NSColor *grid = [NSColor separatorColor];
    NSColor *axis = [NSColor secondaryLabelColor];
    NSColor *label = [NSColor labelColor];
    NSFont *font = [NSFont monospacedSystemFontOfSize:11.0 weight:NSFontWeightRegular];
    NSFont *title_font = [NSFont systemFontOfSize:16.0 weight:NSFontWeightSemibold];

    double x_step = nice_step(_state.x0, _state.x1, 9);
    double y_step = nice_step(_state.y0, _state.y1, 8);
    double first_x = ceil(_state.x0 / x_step - 1e-12) * x_step;
    double first_y = ceil(_state.y0 / y_step - 1e-12) * y_step;

    NSBezierPath *grid_path = [NSBezierPath bezierPath];
    [grid setStroke];
    [grid_path setLineWidth:0.5];
    for (double x = first_x; x <= _state.x1 + x_step * 0.5 && x < first_x + x_step * 40.0;
         x += x_step) {
        NSPoint point = world_to_screen(&_state, plot, x, _state.y0);
        [grid_path moveToPoint:NSMakePoint(point.x, plot.origin.y)];
        [grid_path lineToPoint:NSMakePoint(point.x, NSMaxY(plot))];
    }
    for (double y = first_y; y <= _state.y1 + y_step * 0.5 && y < first_y + y_step * 40.0;
         y += y_step) {
        NSPoint point = world_to_screen(&_state, plot, _state.x0, y);
        [grid_path moveToPoint:NSMakePoint(plot.origin.x, point.y)];
        [grid_path lineToPoint:NSMakePoint(NSMaxX(plot), point.y)];
    }
    [grid_path stroke];

    NSBezierPath *axes = [NSBezierPath bezierPath];
    [axis setStroke];
    [axes setLineWidth:1.0];
    if (_state.x0 <= 0.0 && _state.x1 >= 0.0) {
        NSPoint point = world_to_screen(&_state, plot, 0.0, _state.y0);
        [axes moveToPoint:NSMakePoint(point.x, plot.origin.y)];
        [axes lineToPoint:NSMakePoint(point.x, NSMaxY(plot))];
    }
    if (_state.y0 <= 0.0 && _state.y1 >= 0.0) {
        NSPoint point = world_to_screen(&_state, plot, _state.x0, 0.0);
        [axes moveToPoint:NSMakePoint(plot.origin.x, point.y)];
        [axes lineToPoint:NSMakePoint(NSMaxX(plot), point.y)];
    }
    [axes stroke];

    NSBezierPath *curve = [NSBezierPath bezierPath];
    bool curve_started = false;
    int sample_count = (int)plot.size.width * 3;
    if (sample_count < TERMINALC_PLOT_SAMPLES_MIN)
        sample_count = TERMINALC_PLOT_SAMPLES_MIN;
    if (sample_count > TERMINALC_PLOT_SAMPLES_MAX)
        sample_count = TERMINALC_PLOT_SAMPLES_MAX;
    for (int i = 0; i < sample_count; i++) {
        double t = (double)i / (double)(sample_count - 1);
        double x = _state.x0 + t * (_state.x1 - _state.x0);
        double y;
        if (!sample_y(&_state, x, &y)) {
            curve_started = false;
            continue;
        }
        NSPoint point = world_to_screen(&_state, plot, x, y);
        if (!curve_started)
            [curve moveToPoint:point];
        else
            [curve lineToPoint:point];
        curve_started = true;
    }
    [[NSColor systemGreenColor] setStroke];
    [curve setLineWidth:1.8];
    [curve stroke];

    char tick[32];
    for (double x = first_x; x <= _state.x1 + x_step * 0.5 && x < first_x + x_step * 40.0;
         x += x_step) {
        NSPoint point = world_to_screen(&_state, plot, x, _state.y0);
        format_tick(tick, sizeof tick, x, x_step);
        draw_text(string_from_c(tick), NSMakePoint(point.x - 12.0, plot.origin.y - 24.0),
                  label, font);
    }
    for (double y = first_y; y <= _state.y1 + y_step * 0.5 && y < first_y + y_step * 40.0;
         y += y_step) {
        NSPoint point = world_to_screen(&_state, plot, _state.x0, y);
        format_tick(tick, sizeof tick, y, y_step);
        draw_text(string_from_c(tick), NSMakePoint(8.0, point.y - 6.0), label, font);
    }

    if (_state.hover_on) {
        double x, y;
        screen_to_world(&_state, plot, _state.hover_point, &x, &y);
        double sample;
        if (sample_y(&_state, x, &sample)) {
            NSPoint curve_point = world_to_screen(&_state, plot, x, sample);
            double dx = curve_point.x - _state.hover_point.x;
            double dy = curve_point.y - _state.hover_point.y;
            if (sqrt(dx * dx + dy * dy) <= TERMINALC_PLOT_HOVER_PX) {
                NSBezierPath *crosshair = [NSBezierPath bezierPath];
                [[NSColor systemOrangeColor] setStroke];
                [crosshair moveToPoint:NSMakePoint(curve_point.x - 7.0, curve_point.y)];
                [crosshair lineToPoint:NSMakePoint(curve_point.x + 7.0, curve_point.y)];
                [crosshair moveToPoint:NSMakePoint(curve_point.x, curve_point.y - 7.0)];
                [crosshair lineToPoint:NSMakePoint(curve_point.x, curve_point.y + 7.0)];
                [crosshair stroke];
                _state.hover_x = x;
                _state.hover_y = sample;
                char readout[96];
                snprintf(readout, sizeof readout, "x = %.6g   y = %.6g", x, sample);
                NSPoint text_point = NSMakePoint(curve_point.x + 10.0,
                                                 curve_point.y + 10.0);
                draw_text(string_from_c(readout), text_point,
                          [NSColor systemOrangeColor], font);
            }
        }
    }

    NSString *title = [NSString stringWithFormat:@"TerminalC Plot — %@", string_from_c(_state.expr)];
    draw_text(title, NSMakePoint(TERMINALC_PLOT_MARGIN_L, NSMaxY(bounds) - 30.0),
              label, title_font);
    NSString *subtitle = @"Scroll: zoom   Drag: pan   0: reset   y: auto-fit   q/Esc: close";
    draw_text(subtitle, NSMakePoint(TERMINALC_PLOT_MARGIN_L, 20.0),
              [NSColor secondaryLabelColor], font);
}

- (void)mouseDown:(NSEvent *)event
{
    _state.dragging = YES;
    _state.last_point = [self convertPoint:event.locationInWindow fromView:nil];
    [self.window makeFirstResponder:self];
}

- (void)mouseDragged:(NSEvent *)event
{
    if (!_state.dragging)
        return;
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    NSRect plot = plot_bounds(self.bounds);
    double old_x, old_y, new_x, new_y;
    screen_to_world(&_state, plot, _state.last_point, &old_x, &old_y);
    screen_to_world(&_state, plot, point, &new_x, &new_y);
    double dx = old_x - new_x;
    double dy = old_y - new_y;
    _state.x0 += dx;
    _state.x1 += dx;
    _state.y0 += dy;
    _state.y1 += dy;
    _state.auto_y = false;
    _state.last_point = point;
    [self setNeedsDisplay:YES];
}

- (void)mouseUp:(NSEvent *)event
{
    (void)event;
    _state.dragging = false;
}

- (void)scrollWheel:(NSEvent *)event
{
    NSRect plot = plot_bounds(self.bounds);
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    if (!NSPointInRect(point, plot))
        return;
    double x, y;
    screen_to_world(&_state, plot, point, &x, &y);
    double factor = pow(1.12, event.scrollingDeltaY);
    zoom_about(&_state, x, y, factor);
    _state.auto_y = false;
    [self setNeedsDisplay:YES];
}

- (void)mouseMoved:(NSEvent *)event
{
    _state.hover_point = [self convertPoint:event.locationInWindow fromView:nil];
    _state.hover_on = NSPointInRect(_state.hover_point, plot_bounds(self.bounds));
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent *)event
{
    (void)event;
    _state.hover_on = false;
    [self setNeedsDisplay:YES];
}

- (void)keyDown:(NSEvent *)event
{
    NSString *characters = event.charactersIgnoringModifiers;
    if (characters.length == 0)
        return;
    unichar key = [characters characterAtIndex:0];
    if (key == 0x1b || key == 'q' || key == 'Q') {
        [self.window performClose:nil];
        return;
    }
    if (key == '0') {
        _state.x0 = _state.ix0;
        _state.x1 = _state.ix1;
        _state.auto_y = true;
    } else if (key == 'y' || key == 'Y') {
        _state.auto_y = true;
    } else if (key == '+' || key == '=') {
        NSPoint center = NSMakePoint(NSMidX(plot_bounds(self.bounds)),
                                     NSMidY(plot_bounds(self.bounds)));
        double x, y;
        screen_to_world(&_state, plot_bounds(self.bounds), center, &x, &y);
        zoom_about(&_state, x, y, 1.25);
        _state.auto_y = false;
    } else if (key == '-' || key == '_') {
        NSPoint center = NSMakePoint(NSMidX(plot_bounds(self.bounds)),
                                     NSMidY(plot_bounds(self.bounds)));
        double x, y;
        screen_to_world(&_state, plot_bounds(self.bounds), center, &x, &y);
        zoom_about(&_state, x, y, 0.8);
        _state.auto_y = false;
    } else if (key == 'a' || key == 'A') {
        printf("TerminalC plot view: x = %.6g .. %.6g, y = %.6g .. %.6g\n",
               _state.x0, _state.x1, _state.y0, _state.y1);
        fflush(stdout);
    } else {
        [super keyDown:event];
        return;
    }
    [self setNeedsDisplay:YES];
}

@end

@interface TerminalCPlotWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation TerminalCPlotWindowDelegate

- (void)windowWillClose:(NSNotification *)notification
{
    (void)notification;
    [NSApp stop:nil];
}

@end

bool terminalc_plot_gui(TerminalCCalc *c, const char *expr, double xmin, double xmax)
{
    TerminalCPlotState state;
    memset(&state, 0, sizeof state);
    state.calc = c;
    state.x0 = state.ix0 = xmin;
    state.x1 = state.ix1 = xmax;
    state.y0 = -1.0;
    state.y1 = 1.0;
    state.auto_y = true;
    size_t length = strlen(expr);
    if (length >= sizeof state.expr)
        length = sizeof state.expr - 1;
    memcpy(state.expr, expr, length);
    state.expr[length] = '\0';

    NSApplication *application = [NSApplication sharedApplication];
    [application setActivationPolicy:NSApplicationActivationPolicyRegular];
    [application activateIgnoringOtherApps:YES];

    NSRect frame = NSMakeRect(0.0, 0.0, 1000.0, 680.0);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    NSWindow *window = [[NSWindow alloc] initWithContentRect:frame
                                                     styleMask:style
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
    window.title = @"TerminalC Plot";
    window.minSize = NSMakeSize(640.0, 420.0);
    TerminalCPlotView *view = [[TerminalCPlotView alloc] initWithFrame:frame state:&state];
    window.contentView = view;
    TerminalCPlotWindowDelegate *delegate = [[TerminalCPlotWindowDelegate alloc] init];
    window.delegate = delegate;
    window.acceptsMouseMovedEvents = YES;
    [window center];
    [window makeKeyAndOrderFront:nil];
    [window makeFirstResponder:view];
    [application run];

    [window close];
    [delegate release];
    [view release];
    [window release];
    return true;
}
