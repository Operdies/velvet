// Disassemble CSI escape sequences
#include "collections.h"
#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <utils.h>

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
  enum state { normal, escape, csi, osc, dcs, pnd, charset };
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
      case '(': {
        s = charset;
      } break;
      default: {
        if (isgraph(ch))
          flogmsg(stdout, "ESC %c", ch);
        else
          flogmsg(stdout, "ESC 0x%x", uch);
        s = normal;
      } break;
      }
    } break;
    case charset: {
      logmsg("CHARSET %c", ch);
      s = normal;
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
      if (ch == '\a' || (ch == '\\' && buf[buf_idx - 1] == 0x1b)) {
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
