// gsnake.c - snake in a window, with a keyboard of its own.
//
// The snake you already have (user/prebuilt/snake.elf) draws with ANSI
// escape sequences, so it only works on the serial console. This one draws
// in a window. More to the point, it gets its keys the way your gterm does
// (CS 1690/2670, Quest 5): from a file descriptor of its own, which
// open_console() gives it, and not from file descriptor 0.
//
// Like pong.c and clock.c, this program does its own drawing, so it does not
// depend on the drawing library you wrote in Project 3. Everything it needs
// from your OS is in the WIRING section below, and in the system calls of the
// Syscall ABI that it calls by name: read(), peek(), close(), ticks(), and
// open_console(), without which it will not even link.
//
// Keys: arrow keys or WASD steer, p pauses, q quits.

#include "font8x8.h"
#include "u_common.h"
#include "u_fb.h"

// The board is a grid of square cells, below a line for the score.
#define CELL 12  // pixels per cell; each square is one pixel smaller
#define TOP 14   // pixels above the board
#define MAX_COLS 64
#define MAX_ROWS 64
#define MAX_LEN (MAX_COLS * MAX_ROWS)

// Colors are 0xAABBGGRR.
#define BG_COLOR 0xFF000000u     // black
#define FRAME_COLOR 0xFF808080u  // grey
#define BODY_COLOR 0xFF00FF00u   // green
#define HEAD_COLOR 0xFF00FFFFu   // yellow
#define FOOD_COLOR 0xFF0000FFu   // red
#define TEXT_COLOR 0xFFFFFFFFu   // white

// The snake moves one cell every this many frames (there are 60 a second),
// and speeds up as it grows, down to FASTEST.
#define SLOWEST 8
#define FASTEST 3

// Where we draw: a block of pixels, how big it is, and how far apart its rows
// are. (Our own little struct, as in pong.c, so that it cannot collide with
// whatever your drawing library calls its equivalent.)
struct gs_canvas {
  uint32* pixels;
  int pitch_w;  // distance from one row to the next, in pixels
  int w, h;     // how much of it is ours
};

static struct gs_canvas canvas;
static int con;  // our descriptor on the console: the keyboard

static int cols, rows;  // the size of the board, in cells
static int left, top;   // its top left corner on the canvas, in pixels

// The snake: a ring buffer of the cells it covers, tail first. (Two arrays
// of ints rather than one array of {x, y} structs: with the MMU off, copying
// a small struct can compile to one wide load, which faults on an address
// that is not aligned for it.)
static int snake_x[MAX_LEN], snake_y[MAX_LEN];
static int tail, len;
static bool occupied[MAX_ROWS][MAX_COLS];

static int food_x, food_y;
static int dx, dy;  // where the head goes next
static int score;

// Turns the player asked for that we have not made yet. Two quick key
// presses within one step (up, then left, to double back) must both count.
#define MAX_TURNS 3
static int turn_dx[MAX_TURNS], turn_dy[MAX_TURNS];
static int nturns;

enum key {
  KEY_NONE,
  KEY_UP,
  KEY_DOWN,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_PAUSE,
  KEY_QUIT,
  KEY_AGAIN,
  KEY_OTHER
};

// ---------------------------------------------------------------------------
// WIRING: the places this program talks to your OS, other than reading the
// keyboard. They are the same as in pong.c and clock.c, except for
// gs_open_keyboard().
// ---------------------------------------------------------------------------

static struct canvas* mine;

// Where do we draw? init_canvas(), once: copy the canvas's pixel address,
// width, height and row stride (in pixels) into `canvas`. Returns false if
// there is nowhere to draw.
static bool gs_get_canvas(void) {
  mine = init_canvas();
  if (mine == nullptr) {
    return false;
  }
  canvas.pixels = mine->pixels;
  canvas.pitch_w = (int)mine->pitch_w;
  canvas.w = (int)mine->width;
  canvas.h = (int)mine->height;
  return true;
}

// We have just drawn something: tell the compositor that our canvas has
// changed. (Set the flag *after* drawing, as your own drawing library does.)
static void gs_drawn(void) { mine->changed = 1; }

// Wait until the kernel has next put a frame on the screen.
static void gs_frame_wait(void) { wait_for_vsync(); }

// A keyboard of our own: a new descriptor on the console, or -1.
//
// Once we have it, we let go of the descriptor 0 we were started with, which
// is probably a pipe that our shell's terminal writes into. Holding on to it
// would keep that pipe open after the shell exits, and the terminal would
// find out later than it should, if at all.
static int gs_open_keyboard(void) {
  int fd = open_console();
  if (fd >= 0) {
    close(0);
  }
  return fd;
}

// ---------------------------------------------------------------------------
// The keyboard
// ---------------------------------------------------------------------------

// Turn one byte from the keyboard into a key. An arrow key is three bytes,
// ESC [ A to ESC [ D, which need not arrive in the same read(), so we
// remember how far into one we are.
static enum key decode(char ch) {
  static enum { NORMAL, SAW_ESC, SAW_CSI } state = NORMAL;

  switch (state) {
    case SAW_ESC:
      state = (ch == '[') ? SAW_CSI : NORMAL;
      return KEY_NONE;
    case SAW_CSI:
      state = NORMAL;
      switch (ch) {
        case 'A':
          return KEY_UP;
        case 'B':
          return KEY_DOWN;
        case 'C':
          return KEY_RIGHT;
        case 'D':
          return KEY_LEFT;
        default:
          return KEY_OTHER;
      }
    case NORMAL:
      break;
  }

  switch (ch) {
    case 0x1B:
      state = SAW_ESC;
      return KEY_NONE;
    case 'w':
    case 'W':
      return KEY_UP;
    case 's':
    case 'S':
      return KEY_DOWN;
    case 'a':
    case 'A':
      return KEY_LEFT;
    case 'd':
    case 'D':
      return KEY_RIGHT;
    case 'p':
    case 'P':
      return KEY_PAUSE;
    case 'q':
    case 'Q':
      return KEY_QUIT;
    case 'r':
    case 'R':
      return KEY_AGAIN;
    default:
      return KEY_OTHER;
  }
}

// The next key, if one has been pressed; KEY_NONE if not. Never waits.
static enum key poll_key(void) {
  while (peek(con) > 0) {
    char ch;
    if (read(con, &ch, 1) != 1) {
      return KEY_QUIT;
    }
    enum key k = decode(ch);
    if (k != KEY_NONE) {
      return k;
    }
  }
  return KEY_NONE;
}

// The next key, waiting for one if need be. Waiting in read() blocks us, so
// a paused game costs no CPU at all.
static enum key wait_key(void) {
  for (;;) {
    char ch;
    if (read(con, &ch, 1) != 1) {
      return KEY_QUIT;
    }
    enum key k = decode(ch);
    if (k != KEY_NONE) {
      return k;
    }
  }
}

// ---------------------------------------------------------------------------
// Drawing, and the little bit of string handling we need
// ---------------------------------------------------------------------------

// Fill a rectangle, clipped to the canvas, as in pong.c.
static void gs_fill(int x, int y, int w, int h, uint32 color) {
  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = x + w > canvas.w ? canvas.w : x + w;
  int y1 = y + h > canvas.h ? canvas.h : y + h;

  for (int row = y0; row < y1; row++) {
    uint32* line = &canvas.pixels[row * canvas.pitch_w];
    for (int col = x0; col < x1; col++) {
      line[col] = color;
    }
  }
  gs_drawn();
}

// Text: 8x8 glyphs from font8x8.h, one pixel apart, each pixel of the glyph
// drawn as a `scale` x `scale` square. Bit 0 of each glyph row is its
// leftmost pixel.
#define GLYPH 8
#define ADVANCE(scale) ((GLYPH + 1) * (scale))

static void gs_char(int x, int y, char ch, uint32 color, int scale) {
  if ((unsigned char)ch > 127) {
    return;
  }
  const char* glyph = font8x8_basic[(int)(unsigned char)ch];
  for (int row = 0; row < GLYPH; row++) {
    for (int col = 0; col < GLYPH; col++) {
      if (glyph[row] & (1 << col)) {
        gs_fill(x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

static int gs_strlen(const char* s) {
  int n = 0;
  while (s[n] != '\0') {
    n++;
  }
  return n;
}

static void gs_text(int x, int y, const char* s, uint32 color, int scale) {
  for (; *s != '\0'; s++, x += ADVANCE(scale)) {
    gs_char(x, y, *s, color, scale);
  }
}

static void gs_text_centered(int y, const char* s, uint32 color, int scale) {
  int w = gs_strlen(s) * ADVANCE(scale) - scale;
  gs_text((canvas.w - w) / 2, y, s, color, scale);
}

// Copy `s` to `p`, and return where the NUL went, so that the next piece can
// go there.
static char* gs_append(char* p, const char* s) {
  while (*s != '\0') {
    *p++ = *s++;
  }
  *p = '\0';
  return p;
}

// The same for a number (>= 0), in decimal.
static char* gs_append_number(char* p, int n) {
  char digits[12];
  int k = 0;
  do {
    digits[k++] = (char)('0' + n % 10);
    n /= 10;
  } while (n > 0);
  while (k > 0) {
    *p++ = digits[--k];
  }
  *p = '\0';
  return p;
}

static void draw_cell(int x, int y, uint32 color) {
  gs_fill(left + x * CELL, top + y * CELL, CELL - 1, CELL - 1, color);
}

// Replace the line above the board.
static void status(const char* s) {
  gs_fill(0, 0, canvas.w, TOP - 2, BG_COLOR);
  gs_text(left, 3, s, TEXT_COLOR, 1);
}

static void status_score(void) {
  char buf[40];
  char* p = gs_append(buf, "score ");
  p = gs_append_number(p, score);
  gs_append(p, "   p: pause  q: quit");
  status(buf);
}

// A one-pixel frame around the board, with a pixel of room inside.
static void draw_frame(void) {
  int x = left - 2, y = top - 2;
  int w = cols * CELL + 3, h = rows * CELL + 3;
  gs_fill(x, y, w, 1, FRAME_COLOR);
  gs_fill(x, y + h - 1, w, 1, FRAME_COLOR);
  gs_fill(x, y, 1, h, FRAME_COLOR);
  gs_fill(x + w - 1, y, 1, h, FRAME_COLOR);
}

// ---------------------------------------------------------------------------
// The game
// ---------------------------------------------------------------------------

static uint32 seed;

static int random_below(int n) {
  seed = seed * 1103515245u + 12345u;
  return (int)((seed >> 16) % (uint32)n);
}

// Put the food on a free cell: a random one, or the next free one after it.
// Returns false if there is no free cell -- the snake fills the board.
static bool place_food(void) {
  int start = random_below(cols * rows);
  for (int i = 0; i < cols * rows; i++) {
    int cell = (start + i) % (cols * rows);
    if (!occupied[cell / cols][cell % cols]) {
      food_x = cell % cols;
      food_y = cell / cols;
      draw_cell(food_x, food_y, FOOD_COLOR);
      return true;
    }
  }
  return false;
}

static void add_head(int x, int y) {
  int i = (tail + len) % MAX_LEN;
  snake_x[i] = x;
  snake_y[i] = y;
  len++;
  occupied[y][x] = true;
  draw_cell(x, y, HEAD_COLOR);
}

static void new_game(void) {
  for (int y = 0; y < MAX_ROWS; y++) {
    for (int x = 0; x < MAX_COLS; x++) {
      occupied[y][x] = false;
    }
  }
  gs_fill(0, 0, canvas.w, canvas.h, BG_COLOR);
  draw_frame();

  tail = 0;
  len = 0;
  for (int i = 0; i < 3; i++) {
    if (len > 0) {
      int head = (tail + len - 1) % MAX_LEN;
      draw_cell(snake_x[head], snake_y[head], BODY_COLOR);
    }
    add_head(cols / 4 + i, rows / 2);
  }
  dx = 1;
  dy = 0;
  nturns = 0;
  score = 0;
  place_food();
}

// The player pressed a direction key. Remember the turn, unless it is no
// turn at all (the way we are going already) or would double straight back
// into our own neck.
static void turn(int new_dx, int new_dy) {
  int last_dx = nturns > 0 ? turn_dx[nturns - 1] : dx;
  int last_dy = nturns > 0 ? turn_dy[nturns - 1] : dy;
  if ((new_dx == last_dx && new_dy == last_dy) ||
      (new_dx == -last_dx && new_dy == -last_dy) || nturns == MAX_TURNS) {
    return;
  }
  turn_dx[nturns] = new_dx;
  turn_dy[nturns] = new_dy;
  nturns++;
}

static void steer(enum key k) {
  switch (k) {
    case KEY_UP:
      turn(0, -1);
      break;
    case KEY_DOWN:
      turn(0, 1);
      break;
    case KEY_LEFT:
      turn(-1, 0);
      break;
    case KEY_RIGHT:
      turn(1, 0);
      break;
    default:
      break;
  }
}

// Move one cell. Returns false if the snake ran into a wall or into itself
// (or has filled the whole board).
static bool step(void) {
  if (nturns > 0) {
    dx = turn_dx[0];
    dy = turn_dy[0];
    nturns--;
    for (int i = 0; i < nturns; i++) {
      turn_dx[i] = turn_dx[i + 1];
      turn_dy[i] = turn_dy[i + 1];
    }
  }

  int head = (tail + len - 1) % MAX_LEN;
  int x = snake_x[head] + dx;
  int y = snake_y[head] + dy;
  if (x < 0 || y < 0 || x >= cols || y >= rows) {
    return false;
  }

  bool eats = (x == food_x && y == food_y);
  if (!eats) {
    // The tail moves on first, so the head may follow it into the cell it
    // has just left.
    occupied[snake_y[tail]][snake_x[tail]] = false;
    draw_cell(snake_x[tail], snake_y[tail], BG_COLOR);
    tail = (tail + 1) % MAX_LEN;
    len--;
  }
  if (occupied[y][x]) {
    return false;
  }

  draw_cell(snake_x[head], snake_y[head], BODY_COLOR);
  add_head(x, y);
  if (eats) {
    score++;
    status_score();
    return place_food();
  }
  return true;
}

static int frames_per_step(void) {
  int f = SLOWEST - score / 4;
  return f < FASTEST ? FASTEST : f;
}

// Play one game. Returns false if the player quit in the middle of it.
static bool play(void) {
  status("steer to start, q: quit");
  enum key k = wait_key();
  if (k == KEY_QUIT) {
    return false;
  }
  steer(k);
  status_score();

  int frames = 0;
  for (;;) {
    gs_frame_wait();

    while ((k = poll_key()) != KEY_NONE) {
      if (k == KEY_QUIT) {
        return false;
      }
      if (k == KEY_PAUSE) {
        status("paused - p: go on, q: quit");
        do {
          k = wait_key();
        } while (k != KEY_PAUSE && k != KEY_QUIT);
        if (k == KEY_QUIT) {
          return false;
        }
        status_score();
        continue;
      }
      steer(k);
    }

    if (++frames < frames_per_step()) {
      continue;
    }
    frames = 0;
    if (!step()) {
      return true;
    }
  }
}

static void game_over(void) {
  char buf[24];
  gs_append_number(gs_append(buf, "score "), score);

  int mid = top + rows * CELL / 2;
  gs_fill(left + CELL, mid - 30, cols * CELL - 2 * CELL, 62, BG_COLOR);
  gs_text_centered(mid - 22, "GAME OVER", HEAD_COLOR, 2);
  gs_text_centered(mid + 2, buf, TEXT_COLOR, 1);
  gs_text_centered(mid + 18, "r: again  q: quit", TEXT_COLOR, 1);
  status("");
}

int main(void) {
  // Problems we report the ordinary way, on descriptor 1: to whoever
  // started us.
  if (!gs_get_canvas()) {
    printf("gsnake: no window to draw in (no display, or no room left)\r\n");
    return 1;
  }
  con = gs_open_keyboard();
  if (con < 0) {
    printf("gsnake: cannot open the console\r\n");
    return 1;
  }

  cols = (canvas.w - 4) / CELL;
  rows = (canvas.h - TOP - 4) / CELL;
  cols = cols > MAX_COLS ? MAX_COLS : cols;
  rows = rows > MAX_ROWS ? MAX_ROWS : rows;
  left = (canvas.w - cols * CELL) / 2;
  top = TOP + (canvas.h - TOP - rows * CELL) / 2;
  seed = (uint32)ticks();

  for (;;) {
    new_game();
    if (!play()) {
      break;
    }
    game_over();
    printf("gsnake: game over, score %d\r\n", score);

    enum key k;
    do {
      k = wait_key();
    } while (k != KEY_AGAIN && k != KEY_QUIT);
    if (k == KEY_QUIT) {
      break;
    }
  }
  return 0;
}
