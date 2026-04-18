#include <stdio.h>
#include <stdlib.h>
#include "collections.h"
#include "platform.h"


void pretty_bytes(uint64_t nb, double *rb, char **pf) {
  static char *postfix[] = { "B", "kB", "mB", "gB", "tB" };
  int i = 0;
  *rb = nb;
  while (*rb > 1024) {
    *rb /= 1024;
    i++;
  }
  *pf = postfix[i];
}

static void report(char *name, double secs, uint64_t bytes) {
  double pb;
  char *pfx;
  pretty_bytes(bytes, &pb, &pfx);
  double throughput = (double)bytes / secs;
  double tb;
  char *tpfx;
  pretty_bytes((uint64_t)throughput, &tb, &tpfx);
  printf("%s: %.1f %s in %.1fs (%.1f %s/s)\n", name, pb, pfx, secs, tb, tpfx);
}

uint64_t spam_write(int fd, char *buf, size_t bufsize, uint64_t timeout) {
  uint64_t now = get_ms_since_startup();
  uint64_t written = 0;
  while ((get_ms_since_startup() - now) < timeout)
    written += write(fd, buf, bufsize);
  return written;
}

int main(int argc, char **argv) {
  uint64_t ascii_write = 0;

  int timeout = argc > 1 ? atoi(argv[1]) : 1000;

  char buf[1 << 16];
  char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  for (int i = 0; i < LENGTH(buf); i++) {
    buf[i] = alphabet[i % (sizeof(alphabet) - 1)];
  }

  ascii_write = spam_write(STDOUT_FILENO, buf, LENGTH(buf), timeout);

  uint64_t multibyte_write = 0;
  char wide_alphabet[] = "ø󰬄󰬞󱁬󱉈󰼃󰽗󱑶󰩡󰡾󱤞";
  for (int i = 0; i < LENGTH(buf); i++) {
    buf[i] = wide_alphabet[i % (sizeof(wide_alphabet) - 1)];
  }
  multibyte_write = spam_write(STDOUT_FILENO, buf, LENGTH(buf), timeout);

  printf("\x1b[2J\x1b[H");
  report("ascii", (double)timeout / 1000, ascii_write);
  report("glyphs", (double)timeout / 1000, multibyte_write);

  return 0;
}
