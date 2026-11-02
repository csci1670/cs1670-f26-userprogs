// lines.c - print a few lines of text, and exit.
//
// Provided for CS 1670 Project 4, Quest 5 (CS 1690/2670): something to feed
// into a pipe, as in `lines | upper` or `lines | count`. It writes to
// descriptor 1 with printf(), and neither knows nor cares whether that is
// the console or a pipe.

#include "u_common.h"

static const char* text[] = {
    "Pipes are where the file descriptor abstraction pays off.",
    "A program that reads file descriptor 0 and writes file descriptor 1",
    "does not know whether it is talking to the console,",
    "a pipe, or (in Project 6) a file.",
    "Your OS says hello from the other end of a pipe!",
};

int main(void) {
  for (int i = 0; i < (int)(sizeof(text) / sizeof(text[0])); i++) {
    printf("%s\r\n", text[i]);
  }
  return 0;
}
