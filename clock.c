// clock.c - An analog clock, in the spirit of X11's `xclock`.
//
// Draws a dial with hour, minute and second hands, plus a digital readout
// underneath. The second hand ticks once per second, the way a quartz watch
// does, rather than sweeping continuously -- so the picture only changes
// once a second, which makes this a good program to have on the screen when
// you build damage detection in Quest 5.
//
// This program is deliberately self-contained: apart from finding out where
// to draw, it does all of its own drawing, so it does not depend on the shape
// of the drawing library you write in Quest 3. The only places it touches
// your OS are the four functions in the "WIRING" section below.

#include "font8x8.h"
#include "u_common.h"
#include "u_fb.h"

// ---------------------------------------------------------------------------
// Appearance
// ---------------------------------------------------------------------------

#define BG_COLOR     0xFF201810u  // dark blue-grey
#define FACE_COLOR   0xFFF0F0F0u  // off-white rim and tick marks
#define HOUR_COLOR   0xFFFFFFFFu  // white
#define MINUTE_COLOR 0xFFFFFFFFu  // white
#define SECOND_COLOR 0xFF4040FFu  // red (remember: 0xAABBGGRR)
#define HUB_COLOR    0xFF4040FFu
#define TEXT_COLOR   0xFF00FFFFu  // yellow

// Hand lengths, as a percentage of the dial's radius.
#define HOUR_LEN_PCT   50
#define MINUTE_LEN_PCT 75
#define SECOND_LEN_PCT 85

// Hand half-widths, in pixels. The second hand is drawn as a single line.
#define HOUR_HALFWIDTH   2
#define MINUTE_HALFWIDTH 1

// Target frame interval, in milliseconds. The clock only redraws once a
// second, so this just controls how promptly it notices the second change.
#define FRAME_MS 16

// The clock starts here and counts up from boot. Your OS has no
// battery-backed real-time clock, so there is no way for it to know the
// actual time of day: all it can tell you is how long it has been running.
// 10:09:36 is the time watch advertisements traditionally show, because it
// puts all three hands in different places -- handy while you are debugging
// your rendering.
#define START_HOUR 10
#define START_MIN   9
#define START_SEC  36

// Where we draw: a block of pixels, how big it is, and how far apart its rows
// are. (Our own little struct, so that it cannot collide with whatever your
// drawing library calls its equivalent.)
struct clock_canvas {
  uint32* pixels;
  int pitch_w;  // distance from one row to the next, in pixels
  int w, h;     // how much of it is ours
};

// ---------------------------------------------------------------------------
// WIRING: the four places this program talks to your OS.
//
// As shipped, these work with the OS you have at the end of Quest 3: the
// clock draws straight onto the whole screen and paces itself against the
// timer. Quest 4 gives every process a canvas of its own, and then three of
// these become short changes; the fourth changes in Quest 5. Each comment
// says how.
// ---------------------------------------------------------------------------

// Milliseconds since the machine booted.
//
// We read the ARM generic timer's physical counter directly, with the same
// two system registers your timer driver used in Project 2: CNTPCT_EL0 counts
// up at CNTFRQ_EL0 ticks per second. This works because your processes still
// run at EL1, the same privilege level as your kernel -- a user program on a
// real operating system cannot reach into the hardware like this, and in
// Project 4, when your processes move to EL0, this will stop working and have
// to become a real system call.
//
// If you would rather do it properly now, you already have the pieces: your
// kernel counts timer ticks (you added that in Quest 3 for `uptime`), so
// expose a trampoline function that returns the time and call it here.
static uint64 sys_time_ms(void) {
  uint64 count, freq;
  __asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(count));
  __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
  freq &= 0xFFFFFFFFu;  // the top 32 bits of CNTFRQ_EL0 are reserved
  if (freq == 0) {
    return 0;
  }
  // Scale before dividing, but not so much that a long uptime overflows:
  // count / (freq / 1000) keeps us well inside 64 bits.
  uint64 per_ms = freq / 1000u;
  return (per_ms == 0) ? 0 : count / per_ms;
}

// Where do we draw? Fill in *c and return 1, or return 0 if there is nowhere
// to draw at all.
//
// Quest 3: the whole screen, as your kernel describes it in struct fb_info.
// Quest 4: the canvas your kernel gave this process. Ask for it, and copy
// its pixel address, width, height and row stride (in pixels) into *c.
static int clock_get_canvas(struct clock_canvas* c) {
  struct fb_info* fb = (struct fb_info*)FB_INFO_BASE;
  if (fb->pixels == nullptr) {
    return 0;
  }
  c->pixels = fb->pixels;
  c->pitch_w = (int)(fb->pitch / sizeof(uint32));
  c->w = (int)fb->width;
  c->h = (int)fb->height;
  return 1;
}

// We have just finished drawing a new picture.
//
// Quests 3 and 4: nothing to do. In Quest 3 we drew straight onto the
// screen, and in Quest 4 the compositor copies every canvas every time.
// Quest 5: set your canvas's "changed" flag, so that the compositor knows
// to copy it. Your drawing library sets that flag for programs that use it;
// this program does its own drawing, so it has to say so itself.
static void clock_frame_done(void) {
}

// Wait until it is worth drawing another frame.
//
// Quest 3: there is nothing in your OS yet that knows when the screen is
// redrawn, so the best we can do is pace ourselves against the clock --
// yielding the CPU until FRAME_MS have gone by. That gives a steady animation
// speed, but it is not synchronised with the display in any way.
//
// Quest 4: replace the body with `wait_for_vsync();`. The kernel then wakes
// us just after it has put a new frame on the screen, and we use no CPU at
// all in between.
static void clock_frame_wait(void) {
  static uint64 next = 0;

  uint64 now = sys_time_ms();
  if (next < now) {
    next = now;  // first call, or we fell behind: resynchronise
  }
  next += FRAME_MS;

  while (sys_time_ms() < next) {
    yield();
  }
}

// ---------------------------------------------------------------------------
// Integer trigonometry
//
// The kernel and every user program are compiled with -mgeneral-regs-only, so
// there is no floating point available: no `sin`, no `cos`, no doubles. That
// is normal for OS code, and it is not a problem -- a table of 91 integers and
// a little symmetry gets us all the precision a 200-pixel dial can show.
//
// SIN_TABLE[d] is sin(d degrees) * 1024, for d = 0..90.
// ---------------------------------------------------------------------------

#define TRIG_ONE 1024

static const int16 SIN_TABLE[91] = {
       0,   18,   36,   54,   71,   89,  107,  125,  143,  160,
     178,  195,  213,  230,  248,  265,  282,  299,  316,  333,
     350,  367,  384,  400,  416,  433,  449,  465,  481,  496,
     512,  527,  543,  558,  573,  587,  602,  616,  630,  644,
     658,  672,  685,  698,  711,  724,  737,  749,  761,  773,
     784,  796,  807,  818,  828,  839,  849,  859,  868,  878,
     887,  896,  904,  912,  920,  928,  935,  943,  949,  956,
     962,  968,  974,  979,  984,  989,  994,  998, 1002, 1005,
    1008, 1011, 1014, 1016, 1018, 1020, 1022, 1023, 1023, 1024,
    1024,
};

// sin(deg) * 1024, for any integer number of degrees.
static int isin(int deg) {
  deg %= 360;
  if (deg < 0) {
    deg += 360;
  }
  if (deg <= 90) {
    return SIN_TABLE[deg];
  }
  if (deg <= 180) {
    return SIN_TABLE[180 - deg];
  }
  if (deg <= 270) {
    return -SIN_TABLE[deg - 180];
  }
  return -SIN_TABLE[360 - deg];
}

// cos(deg) * 1024.
static int icos(int deg) {
  return isin(deg + 90);
}

// ---------------------------------------------------------------------------
// Drawing primitives
//
// Everything here writes through a `struct clock_canvas`, clipped to its
// edges, so a bug in the geometry cannot scribble on somebody else's pixels
// (or off the end of the screen).
// ---------------------------------------------------------------------------

// Set one pixel. Anything outside the canvas is silently dropped -- which is
// what makes every function below safe.
static void clk_pixel(struct clock_canvas* c, int x, int y, uint32 color) {
  if (x < 0 || y < 0 || x >= c->w || y >= c->h) {
    return;
  }
  c->pixels[y * c->pitch_w + x] = color;
}

// Clear the whole canvas. This runs once a second over every pixel we own, so
// unlike everything else here it is worth writing the rows directly rather
// than going through the bounds-checked clk_pixel().
static void clk_fill(struct clock_canvas* c, uint32 color) {
  for (int row = 0; row < c->h; row++) {
    uint32* line = &c->pixels[row * c->pitch_w];
    for (int col = 0; col < c->w; col++) {
      line[col] = color;
    }
  }
}

// A line from (x0, y0) to (x1, y1), using Bresenham's algorithm: at each step
// we move one pixel along the dominant axis and decide, using only integer
// arithmetic, whether to step along the other one too.
static void clk_line(struct clock_canvas* c, int x0, int y0, int x1, int y1,
                     uint32 color) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  int sx = (dx >= 0) ? 1 : -1;
  int sy = (dy >= 0) ? 1 : -1;
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;

  int err = dx - dy;
  for (;;) {
    clk_pixel(c, x0, y0, color);
    if (x0 == x1 && y0 == y1) {
      return;
    }
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// A thick line, drawn as (2 * halfwidth + 1) parallel thin ones offset
// perpendicular to its direction. Crude, but the hands are short enough that
// it looks fine and it keeps the code to four lines.
static void clk_thick_line(struct clock_canvas* c, int x0, int y0, int x1,
                           int y1, int halfwidth, uint32 color) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  // Step perpendicular to the line: along x if it is mostly vertical,
  // along y if it is mostly horizontal.
  int px = (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx) ? 1 : 0;
  int py = px ? 0 : 1;

  for (int i = -halfwidth; i <= halfwidth; i++) {
    clk_line(c, x0 + i * px, y0 + i * py, x1 + i * px, y1 + i * py, color);
  }
}

// A circle outline, using the midpoint circle algorithm: compute one octant
// and mirror it into the other seven.
static void clk_circle(struct clock_canvas* c, int cx, int cy, int r,
                       uint32 color) {
  int x = r;
  int y = 0;
  int err = 1 - r;

  while (x >= y) {
    clk_pixel(c, cx + x, cy + y, color);
    clk_pixel(c, cx + y, cy + x, color);
    clk_pixel(c, cx - y, cy + x, color);
    clk_pixel(c, cx - x, cy + y, color);
    clk_pixel(c, cx - x, cy - y, color);
    clk_pixel(c, cx - y, cy - x, color);
    clk_pixel(c, cx + y, cy - x, color);
    clk_pixel(c, cx + x, cy - y, color);

    y++;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}

// A filled disc, drawn as a stack of horizontal spans.
static void clk_disc(struct clock_canvas* c, int cx, int cy, int r,
                     uint32 color) {
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      if (dx * dx + dy * dy <= r * r) {
        clk_pixel(c, cx + dx, cy + dy, color);
      }
    }
  }
}

// One character of the 8x8 bitmap font, scaled up by `scale`. The font stores
// each glyph as eight bytes, one per row, with bit 0 the leftmost pixel.
static void clk_glyph(struct clock_canvas* c, int x, int y, char ch,
                      uint32 color, int scale) {
  if ((unsigned char)ch > 127) {
    return;
  }
  const char* glyph = font8x8_basic[(int)(unsigned char)ch];
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      if (!(glyph[row] & (1 << col))) {
        continue;
      }
      for (int sy = 0; sy < scale; sy++) {
        for (int sx = 0; sx < scale; sx++) {
          clk_pixel(c, x + col * scale + sx, y + row * scale + sy, color);
        }
      }
    }
  }
}

static void clk_text(struct clock_canvas* c, int x, int y, const char* s,
                     uint32 color, int scale) {
  int advance = 8 * scale + scale;
  while (*s) {
    clk_glyph(c, x, y, *s, color, scale);
    x += advance;
    s++;
  }
}

// ---------------------------------------------------------------------------
// The clock itself
// ---------------------------------------------------------------------------

struct dial {
  int cx, cy;       // centre, in canvas coordinates
  int r;            // radius in pixels
  int text_y;       // top of the digital readout, below the dial
  int text_scale;   // font magnification for the readout
};

// Work out how big a dial fits on the canvas we were given, reserving a band
// at the bottom for the digital readout.
static void dial_layout(struct dial* d, int w, int h) {
  d->text_scale = (w >= 320) ? 2 : 1;

  int text_h = 8 * d->text_scale;
  int band = text_h + 14;       // readout, plus a little breathing room
  int face_h = h - band;
  if (face_h < 20) {            // a very short canvas: drop the readout
    face_h = h;
    band = 0;
  }

  int limit = (w < face_h ? w : face_h) / 2;
  int margin = limit / 20;      // keep the rim clear of the window edge
  if (margin < 6) {
    margin = 6;
  }
  d->r = limit - margin;
  if (d->r < 8) {
    d->r = limit;               // tiny window: every pixel counts
  }

  d->cx = w / 2;
  d->cy = face_h / 2;
  d->text_y = (band == 0) ? (d->cy + d->r / 2) : (face_h + 4);
}

// Convert "this many degrees clockwise from twelve o'clock, this far out from
// the centre" into a pixel position.
//
// Note the minus sign on y: screen coordinates grow downwards, while the
// clock's angles are measured the way we think about them, so the vertical
// component has to be flipped.
static void polar(const struct dial* d, int deg, int len, int* x, int* y) {
  *x = d->cx + (len * isin(deg)) / TRIG_ONE;
  *y = d->cy - (len * icos(deg)) / TRIG_ONE;
}

static void clk_face(struct clock_canvas* c, const struct dial* d) {
  clk_circle(c, d->cx, d->cy, d->r, FACE_COLOR);
  clk_circle(c, d->cx, d->cy, d->r - 1, FACE_COLOR);

  // Twelve hour marks, every 30 degrees. The marks at 12, 3, 6 and 9 are
  // drawn longer so that the dial is readable at a glance.
  for (int hour = 0; hour < 12; hour++) {
    int deg = hour * 30;
    int inner = (hour % 3 == 0) ? (d->r * 82) / 100 : (d->r * 90) / 100;
    int x0, y0, x1, y1;
    polar(d, deg, inner, &x0, &y0);
    polar(d, deg, (d->r * 97) / 100, &x1, &y1);
    clk_thick_line(c, x0, y0, x1, y1, (hour % 3 == 0) ? 1 : 0, FACE_COLOR);
  }
}

static void clk_hands(struct clock_canvas* c, const struct dial* d,
                      int hour, int minute, int second) {
  // The hour and minute hands do not jump: at half past, the hour hand sits
  // halfway between the two numerals, which is what makes an analog clock
  // readable between the marks.
  int hour_deg = (hour % 12) * 30 + minute / 2;
  int minute_deg = minute * 6 + second / 10;
  int second_deg = second * 6;

  int x, y;

  polar(d, hour_deg, (d->r * HOUR_LEN_PCT) / 100, &x, &y);
  clk_thick_line(c, d->cx, d->cy, x, y, HOUR_HALFWIDTH, HOUR_COLOR);

  polar(d, minute_deg, (d->r * MINUTE_LEN_PCT) / 100, &x, &y);
  clk_thick_line(c, d->cx, d->cy, x, y, MINUTE_HALFWIDTH, MINUTE_COLOR);

  polar(d, second_deg, (d->r * SECOND_LEN_PCT) / 100, &x, &y);
  clk_line(c, d->cx, d->cy, x, y, SECOND_COLOR);
  // A short counterweight on the far side, as a real second hand has.
  polar(d, second_deg + 180, d->r / 6, &x, &y);
  clk_line(c, d->cx, d->cy, x, y, SECOND_COLOR);

  clk_disc(c, d->cx, d->cy, 3, HUB_COLOR);
}

// Format `value` as exactly two digits into buf[0] and buf[1].
static void two_digits(char* buf, int value) {
  buf[0] = (char)('0' + (value / 10) % 10);
  buf[1] = (char)('0' + value % 10);
}

static void clk_digital(struct clock_canvas* c, const struct dial* d,
                        int hour, int minute, int second) {
  char text[9];
  two_digits(&text[0], hour);
  text[2] = ':';
  two_digits(&text[3], minute);
  text[5] = ':';
  two_digits(&text[6], second);
  text[8] = '\0';

  // Centre the readout horizontally. Each character occupies 9 pixels at
  // scale 1 (8 for the glyph, 1 for the gap), so 9 * scale per character.
  int text_w = 8 * (8 * d->text_scale + d->text_scale);
  clk_text(c, d->cx - text_w / 2, d->text_y, text, TEXT_COLOR,
           d->text_scale);
}

int main(void) {
  struct clock_canvas canvas;
  if (!clock_get_canvas(&canvas)) {
    // Nowhere to draw: we were started with `make qemu` rather than
    // `make qemu-fb`, or the kernel did not give us a window. Say so once and
    // then get out of the scheduler's way.
    printf("clock: nowhere to draw (try 'make qemu-fb')\r\n");
    for (;;) {
      yield();
    }
  }

  struct dial dial;
  dial_layout(&dial, canvas.w, canvas.h);

  uint64 boot_offset_s =
      (uint64)START_HOUR * 3600u + (uint64)START_MIN * 60u + START_SEC;

  // Force the first frame to draw.
  uint64 last_second = (uint64)-1;

  for (;;) {
    uint64 now_s = sys_time_ms() / 1000u + boot_offset_s;

    if (now_s != last_second) {
      last_second = now_s;

      int second = (int)(now_s % 60u);
      int minute = (int)((now_s / 60u) % 60u);
      int hour = (int)((now_s / 3600u) % 24u);

      // Redraw the whole dial. This is the simplest thing that is correct.
      // Drawn straight onto the screen, as in Quest 3, it means that for a
      // few milliseconds every second the dial is half-erased in the very
      // memory the display is showing.
      clk_fill(&canvas, BG_COLOR);
      clk_face(&canvas, &dial);
      clk_hands(&canvas, &dial, hour, minute, second);
      clk_digital(&canvas, &dial, hour, minute, second);
      clock_frame_done();
    }

    clock_frame_wait();
  }
}
