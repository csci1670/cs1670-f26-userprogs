// upper.c - a filter: copy descriptor 0 to descriptor 1 until end-of-file,
// turning lower-case letters into upper case on the way.
//
// Provided for CS 1670 Project 4, Quest 5 (CS 1690/2670): try
// `lines | upper`. Like every filter, it runs until read() returns 0, which
// on a pipe means that the pipe is empty and no write end is open anywhere
// -- so if some process still holds a write end, upper waits for ever.
//
// It needs your read() and write() system calls (Quest 3), declared in
// user/lib/u_syscalls.h as in the Syscall ABI.

#include "u_common.h"

int main(void) {
  char buf[128];
  for (;;) {
    int64 n = read(0, buf, sizeof(buf));
    if (n <= 0) {
      return n == 0 ? 0 : 1;  // end-of-file, or an error
    }
    for (int64 i = 0; i < n; i++) {
      if (buf[i] >= 'a' && buf[i] <= 'z') {
        buf[i] = (char)(buf[i] - 'a' + 'A');
      }
    }
    // A write to a pipe that nobody reads any more fails; there is no point
    // carrying on then.
    if (write(1, buf, (uint64)n) < 0) {
      return 1;
    }
  }
}
