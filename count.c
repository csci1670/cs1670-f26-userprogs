// count.c - a filter: read descriptor 0 until end-of-file, then print how many
// lines and characters there were, like `wc -lc`.
//
// Provided for CS 1670 Project 4, Quest 5 (CS 1690/2670): try `lines | count`
// or `squares | count`. It prints nothing until its input ends, so it is a
// good test of end-of-file: if a pipeline ending in count never prints, a
// write end is still open somewhere.
//
// It needs your read() system call (Quest 3), declared in
// user/lib/u_syscalls.h as in the Syscall ABI.

#include "u_common.h"

int main(void) {
  char buf[128];
  int lines = 0, chars = 0;
  for (;;) {
    int64 n = read(0, buf, sizeof(buf));
    if (n < 0) {
      return 1;
    }
    if (n == 0) {
      break;  // end-of-file
    }
    chars += (int)n;
    for (int64 i = 0; i < n; i++) {
      if (buf[i] == '\n') {
        lines++;
      }
    }
  }
  printf("%d lines, %d characters\r\n", lines, chars);
  return 0;
}
