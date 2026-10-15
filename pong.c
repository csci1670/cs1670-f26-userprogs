// pong.c - A box that bounces around the screen, like an old DVD screensaver.
//
// There is nothing clever here. The point of this program is to move
// something big, quickly, so that you can *see* what goes wrong when a
// program draws straight into the memory the display is reading from: in
// Quest 3 the box will tear, and in Quest 4 you will make it stop.
//
// Like clock.c, this program does its own drawing, so it does not depend on
// the drawing library you write in Quest 3. Everything it needs from your OS
// is in the WIRING section below.

#include "u_common.h"
#include "u_fb.h"

// ---------------------------------------------------------------------------
// Appearance and behaviour
// ---------------------------------------------------------------------------

#define BG_COLOR  0xFF000000u  // black
#define BOX_COLOR 0xFFFFFFFFu  // white (0xAABBGGRR)

// Box size, in pixels. Big enough to make tearing easy to see, and small
// enough to bounce around comfortably inside a Quest 4 window.
#define BOX_W 120
#define BOX_H 90

// Pixels moved per frame. The further the box moves between two frames, the
// bigger the step in a torn frame -- try smaller values in Quest 3 and see
// how much harder the tearing is to spot.
#define STEP_X 12
#define STEP_Y 8

// Target frame interval, in milliseconds (16 ms is about 60 frames/second).
#define FRAME_MS 16

// Where we draw: a block of pixels, how big it is, and how far apart its rows
// are. (Our own little struct, so that it cannot collide with whatever your
// drawing library calls its equivalent.)
struct pong_canvas {
  uint32* pixels;
  int pitch_w;  // distance from one row to the next, in pixels
  int w, h;     // how much of it is ours
};

// ---------------------------------------------------------------------------
// WIRING: the four places this program talks to your OS.
//
// These are the same four as in clock.c -- read the comments there for the
// full explanation of each. As shipped they work with the OS you have at the
// end of Quest 3; three become short changes in Quest 4, the fourth in
// Quest 5.
// ---------------------------------------------------------------------------

// Milliseconds since boot, read straight from the ARM generic timer. See the
// long comment in clock.c for why a user program is able to do this today,
// and why it will have to become a system call in Project 4.
//
// (Only pong_frame_wait() uses this. Once Quest 4 replaces that with
// wait_for_vsync(), nothing does -- hence [[maybe_unused]], which stops the
// compiler treating that as an error.)
[[maybe_unused]] static uint64 sys_time_ms(void) {
  uint64 count, freq;
  __asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(count));
  __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
  freq &= 0xFFFFFFFFu;
  uint64 per_ms = freq / 1000u;
  return (per_ms == 0) ? 0 : count / per_ms;
}

// Where do we draw? Fill in *c and return 1, or return 0 if there is nowhere
// to draw at all.
//
// Quest 3: the whole screen, as your kernel describes it in struct fb_info.
// Quest 4: the canvas your kernel gave this process.
static int pong_get_canvas(struct pong_canvas* c) {
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

// We have just finished drawing a frame.
//
// Quests 3 and 4: nothing to do. Quest 5: set your canvas's "changed" flag.
static void pong_frame_done(void) {
}

// Wait until it is worth drawing another frame.
//
// Quest 3: pace ourselves against the clock, yielding the CPU until FRAME_MS
// have gone by. Nothing about this is synchronised with the display, which
// is exactly why you will see the box tear.
//
// Quest 4: replace the body with `wait_for_vsync();`.
static void pong_frame_wait(void) {
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
// Drawing
// ---------------------------------------------------------------------------

// Fill a rectangle, clipped to the canvas. The clipping is what stops a bug
// in the bouncing arithmetic from scribbling over somebody else's pixels.
static void pong_fill(struct pong_canvas* c, int x, int y, int w, int h,
                      uint32 color) {
  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = x + w > c->w ? c->w : x + w;
  int y1 = y + h > c->h ? c->h : y + h;

  for (int row = y0; row < y1; row++) {
    uint32* line = &c->pixels[row * c->pitch_w];
    for (int col = x0; col < x1; col++) {
      line[col] = color;
    }
  }
}

// ---------------------------------------------------------------------------

int main(void) {
  struct pong_canvas canvas;
  if (!pong_get_canvas(&canvas)) {
    printf("pong: nowhere to draw (try 'make qemu-fb')\r\n");
    for (;;) {
      yield();
    }
  }

  // Start in the top-left corner, moving down and to the right.
  int x = 0, y = 0;
  int vx = STEP_X, vy = STEP_Y;

  pong_fill(&canvas, 0, 0, canvas.w, canvas.h, BG_COLOR);

  for (;;) {
    pong_frame_wait();

    // Erase the box where it was...
    pong_fill(&canvas, x, y, BOX_W, BOX_H, BG_COLOR);

    // ...move it, bouncing off each edge...
    x += vx;
    y += vy;
    if (x < 0) {
      x = 0;
      vx = -vx;
    }
    if (y < 0) {
      y = 0;
      vy = -vy;
    }
    if (x + BOX_W > canvas.w) {
      x = canvas.w - BOX_W;
      vx = -vx;
    }
    if (y + BOX_H > canvas.h) {
      y = canvas.h - BOX_H;
      vy = -vy;
    }

    // ...and draw it where it is now.
    //
    // The display hardware does not know or care that we are in the middle
    // of this. It reads the screen top to bottom, continuously, so a frame
    // it shows can easily contain the top of the box from one of our frames
    // and the bottom from the next. That is tearing.
    pong_fill(&canvas, x, y, BOX_W, BOX_H, BOX_COLOR);

    pong_frame_done();
  }
}
