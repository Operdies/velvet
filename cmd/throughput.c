#include <stdio.h>
#include "collections.h"
#include "platform.h"


#define NUM_WRITES 10000

void pretty_bytes(uint64_t nb, uint64_t *rb, char **pf) {
  static char *postfix[] = { "B", "kB", "mB", "gB", "tB" };
  int i = 0;
  while (nb > 1024) {
    nb /= 1024;
    i++;
  }
  *pf = postfix[i];
  *rb = nb;
}

int main(void) {
  uint64_t ascii_bytes;
  char *ascii_prefix;
  double ascii_secs;

  char buf[1 << 16];
  {
    char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; i < LENGTH(buf); i++) {
      buf[i] = alphabet[i % LENGTH(alphabet)];
    }

    uint64_t now = get_ms_since_startup();

    for (int i = 0; i < NUM_WRITES; i++)
      write(STDOUT_FILENO, buf, LENGTH(buf));

    uint64_t end = get_ms_since_startup();
    ascii_secs = ((double)end - now) / 1000;
    pretty_bytes(NUM_WRITES * LENGTH(buf), &ascii_bytes, &ascii_prefix);
  }

  uint64_t multibyte_bytes;
  char *multibyte_prefix;
  double multibyte_secs;
  {
    char alphabet[] = "ø󰬄󰬞󱁬󱉈󰼃󰽗󱑶󰩡󰡾󱤞";
    for (int i = 0; i < LENGTH(buf); i++) {
      buf[i] = alphabet[i % LENGTH(alphabet)];
    }

    uint64_t now = get_ms_since_startup();

    for (int i = 0; i < NUM_WRITES; i++)
      write(STDOUT_FILENO, buf, LENGTH(buf));

    uint64_t end = get_ms_since_startup();
    multibyte_secs = ((double)end - now) / 1000;
    pretty_bytes(NUM_WRITES * LENGTH(buf), &multibyte_bytes, &multibyte_prefix);
  }

  printf("\n\nPure ascii: %llu%s in %fs\n", ascii_bytes, ascii_prefix, ascii_secs);
  printf("\n\nPure multibyte: %llu%s in %fs\n", multibyte_bytes, multibyte_prefix, multibyte_secs);

  return 0;
}
