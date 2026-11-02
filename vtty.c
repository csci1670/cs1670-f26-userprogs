// vtty.c - a terminal for your shell on the serial console.
//
// CS 1690/2670, Quest 5. Like `getty` on Unix, vtty owns a terminal -- here,
// the serial console -- and runs a shell on it: it makes two pipes, starts
// `sh` with one pipe's read end as its standard input and the other pipe's
// write end as its standard output, and then sits between the shell and the
// serial console for as long as the shell runs. If the shell ever exits,
// vtty starts a new one.
//
//                         to_sh                 from_sh
//     serial  ──► vtty ──► pipe ──► sh ──► pipe ──► vtty ──►  serial
//     keyboard                                                console
//
// vtty is deliberately dumb: it never looks at what passes through it. Every
// byte typed on the serial console goes into the shell's input pipe as soon
// as it arrives, and every byte the shell -- or any program the shell
// starts, which inherits the shell's descriptors -- writes comes out on the
// serial console unchanged. So the shell, and every program it runs, sees
// exactly what a program reading the console directly would see: raw
// keystrokes, one at a time, with no echo. Echoing and editing the command
// line is the shell's job, and a program like snake, which wants every key
// the moment it is pressed, works under vtty just as it does on the bare
// console.
//
// Your graphical terminal emulator (gterm) does the same job for a window.
//
// vtty uses only system calls from the Syscall ABI, by their names there,
// and your printf(); so it builds once you have implemented pipe() and
// close(). Start it from init() once your pipes and your shell work.
//
// The cost of being simple: we have no way to wait for "the keyboard *or*
// the shell has something for us" (no select() or poll()), so vtty checks
// each with peek() and yields when neither has anything. It never blocks,
// which means it is always runnable, and your idle process will no longer
// get to run while vtty is alive.

#include "u_common.h"

#define SHELL "sh"

// Our own standard input and output: the serial console, where init() put
// them.
#define CONSOLE_IN 0
#define CONSOLE_OUT 1

#define CHUNK 128

// Write all n bytes of buf to fd. Returns false if fd stops accepting them
// (for a pipe: nobody reads it any more).
static bool write_all(int fd, const char* buf, uint64 n) {
  while (n > 0) {
    int64 written = write(fd, buf, n);
    if (written <= 0) {
      return false;
    }
    buf += written;
    n -= (uint64)written;
  }
  return true;
}

// Move whatever is ready from `from` to `to`, at most one chunk of it.
// Returns 1 if we moved something, 0 if there was nothing to move, and -1
// if the other side has gone: `from` is at end-of-file (peek() says -1), or
// `to` would not take it.
static int move_chunk(int from, int to) {
  int64 avail = peek(from);
  if (avail < 0) {
    return -1;
  }
  if (avail == 0) {
    return 0;
  }
  char buf[CHUNK];
  int64 n = read(from, buf, avail < CHUNK ? (uint64)avail : CHUNK);
  if (n <= 0) {
    return 0;
  }
  return write_all(to, buf, (uint64)n) ? 1 : -1;
}

// Sit between the serial console and the shell: copy its output to the
// console, and the keyboard to its input, until the shell has exited. We
// find out in one of two ways. Its output pipe reaches end-of-file once the
// shell, and every program it started that shared its output, has exited.
// Or a key we pass on cannot go into its input pipe, because nobody reads
// that any more: that is how we notice when a program the shell left
// running in the background still holds its output.
static void relay(int to_sh, int from_sh) {
  for (;;) {
    int out = move_chunk(from_sh, CONSOLE_OUT);
    int in = move_chunk(CONSOLE_IN, to_sh);
    if (out < 0 || in < 0) {
      return;
    }
    if (out == 0 && in == 0) {
      yield();
    }
  }
}

int main(void) {
  for (;;) {
    int to_sh[2], from_sh[2];
    if (pipe(to_sh) < 0) {
      printf(
          "vtty: cannot make a pipe (does your kernel implement pipe()?)\r\n");
      return 1;
    }
    if (pipe(from_sh) < 0) {
      printf("vtty: cannot make a pipe\r\n");
      close(to_sh[0]);
      close(to_sh[1]);
      return 1;
    }

    int pid = spawn(SHELL, to_sh[0], from_sh[1]);

    // Those two ends are the shell's now. If we kept our copies, the shell
    // could never see end-of-file from us, and we could never notice that it
    // has gone: our writes into to_sh would still find a reader -- us.
    close(to_sh[0]);
    close(from_sh[1]);

    if (pid < 0) {
      printf("vtty: cannot start %s\r\n", SHELL);
      close(to_sh[1]);
      close(from_sh[0]);
      return 1;
    }

    relay(to_sh[1], from_sh[0]);

    // The shell is gone. Show whatever it wrote last, collect it, and start
    // a new one, as getty would.
    close(to_sh[1]);
    while (move_chunk(from_sh[0], CONSOLE_OUT) > 0) {
    }
    close(from_sh[0]);
    int status = wait(pid);
    printf("\r\n[vtty: %s exited with status %d; starting a new one]\r\n",
           SHELL, status);
  }
}
