// Disassemble CSI escape sequences
#include "collections.h"
#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <utils.h>

static inline uint8_t utf8_expected_length(uint8_t ch) {
  if ((ch & 0x80) == 0x00)
    return 1; /* 0xxxxxxx */
  else if ((ch & 0xE0) == 0xC0)
    return 2; /* 110xxxxx */
  else if ((ch & 0xF0) == 0xE0)
    return 3; /* 1110xxxx */
  else if ((ch & 0xF8) == 0xF0)
    return 4; /* 11110xxx */
  else
    return 0; /* invalid leading byte or continuation byte */
}

struct string symbuf = {0};
static void flush_symbuf(void) {
  if (symbuf.len) {
    flogmsg(stdout, "LITERAL '%.*s'", symbuf.len, symbuf.content);
    string_clear(&symbuf);
  }
}

static void disassemble(FILE *f) {
  char buf[1024] = {0};
  int buf_idx = 0;
  char ch;
  unsigned char uch;
  enum state { normal, escape, csi, osc, dcs, pnd };
  enum state s = normal;
  while ((ch = fgetc(f)) != EOF) {
    uch = (unsigned char)ch;
    switch (s) {
    case normal: {
      if (ch == 0x1b) {
        flush_symbuf();
        s = escape;
        break;
      } else if (ch == '\r') {
        flush_symbuf();
        flogmsg(stdout, "CARRIAGE");
        break;
      } else if (ch == '\n') {
        flush_symbuf();
        flogmsg(stdout, "NEWLINE");
        break;
      }

      if ((uch & 0x80)) {
        string_push_char(&symbuf, ch);
      } else {
        if (isgraph(ch) || ch == ' ') {
          string_push_char(&symbuf, ch);
        } else {
          int ctrl_mask = 037;
          uint8_t reversed = (uch & ctrl_mask) + 64;
          if (ch <= ctrl_mask && isgraph(reversed)) {
            string_push_char(&symbuf, '^');
            string_push_char(&symbuf, reversed);
          } else {
            string_push_char(&symbuf, '.');
          }
        }
      }
    } break;
    case escape: {
      switch (ch) {
      case '[': {
        s = csi;
      } break;
      case '#': {
        s = pnd;
      } break;
      case ']': {
        s = osc;
      } break;
      case 'P': {
        s = dcs;
      } break;
      default: {
        if (isgraph(ch))
          flogmsg(stdout, "CSI %c", ch);
        else
          flogmsg(stdout, "CSI 0x%x", uch);
        s = normal;
      } break;
      }
    } break;
    case dcs: {
      if (ch == '\\' && buf[buf_idx - 1] == 0x1b) {
        flogmsg(stdout, "DCS %.*s", buf_idx - 1, buf);
        buf_idx = 0;
        s = normal;
      } else {
        buf[buf_idx++] = ch;
      }
    } break;
    case csi: {
      buf[buf_idx++] = ch;
      if (ch >= 0x40 && ch <= 0x7E) {
        flogmsg(stdout, "CSI %.*s", buf_idx, buf);
        buf_idx = 0;
        s = normal;
      }
    } break;
    case osc: {
      if (ch == '\a') {
        flogmsg(stdout, "OSC %.*s", buf_idx, buf);
        buf_idx = 0;
        s = normal;
      } else {
        buf[buf_idx++] = ch;
      }
    } break;
    default: {
      die("Unhandled switch case");
    } break;
    }
  }
  flush_symbuf();
}

int main(int argc, char **argv) {
  if (argc < 2) {
    disassemble(stdin);
  } else {
    for (int i = 1; i < argc; i++) {
      FILE *f = fopen(argv[i], "r");
      if (f) {
        disassemble(f);
        fclose(f);
      }
    }
  }
}
