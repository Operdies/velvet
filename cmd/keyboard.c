#include <stdio.h>
#include "text.h"
#include "utils.h"
#include "velvet.h"
#include <string.h>
#include <sys/signal.h>
#include <velvet_api.h>

struct key {
  char *text;
  float width;
  char *name;
};

struct keyboard {
  struct key layout[6][15];
};

#define K6(t, w, n){.text = t, .width = w, .name = n}
#define K5(t, w, n){.text = #t, .width = w, .name = n}
#define K4(t, n){.text = t, .width = 1.0f, .name = #n}
#define K3(t){.text = t, .width = 1.0f, .name = t}
#define K2(t, w){.text = #t, .width = w, .name = #t}
#define K(t) K2(t, 1.0f)

static int keywidth(struct key *k) {
  return (int)(k->width * 8) - 2;
}

static struct keyboard kbd = {{
  {K2(ESC, 1.5), K(F1), K(F2), K(F3), K(F4), K(F5), K(F6), K(F7), K(F8), K(F9), K(F10), K(F11), K(F12), K5(DEL, 1.0, "DELETE")},
  {K3("§"), K(1), K(2), K(3), K(4), K(5), K(6), K(7), K(8), K(9), K(0), K(-), K(=), K6("⌫", 1.5, "BACKSPACE")},
  {K6("↹", 1.5, "TAB"), K(Q), K(W), K(E), K(R), K(T), K(Y), K(U), K(I), K(O), K(P), K([), K(]), K2(\\, 1)},
  {K6("⇪", 2.0, "CAPS_LOCK"), K(A), K(S), K(D), K(F), K(G), K(H), K(J), K(K), K(L), K(;), K3("'"), K6("↵", 1.5, "RETURN") },
  {K6("⇧", 1.5, "LEFT_SHIFT"), K(`), K(Z), K(X), K(C), K(V), K(B), K(N), K(M), K3(","), K(.), K(/), K4("↑", UP), K6("⇧", 1, "RIGHT_SHIFT") },
  {K(FN), K6("^", 1.0, "LEFT_CONTROL"), K6("⌥", 1.0, "LEFT_ALT"), K6("⌘", 1.25, "LEFT_SUPER"), K2(SPACE, 5),
    K6("⌘", 1.25, "RIGHT_SUPER"), K6("⌥", 1.0, "RIGHT_ALT"), K4("←", LEFT), K4("↓", DOWN), K4("→", RIGHT) },
}};

static char *styles[6][15] = { 0 };

static void draw_keyboard() {
  static struct string b = {0};
  string_clear(&b);
  char *spaces =
      "                                                                                                           ";
  char *dashes =
      "───────────────────────────────────────────────────────────────────────────────────────────────────────────";
  char *topleft = "┌";
  char *topright = "┐";
  char *bottomleft = "└";
  char *bottomright = "┘";
  char *pipe = "│";
  char *dash = "─";

  int i = 0;
  string_push_format_slow(&b, "\x1b[m\x1b[H\x1b[2J");
  for (; i < 6; i++) {
    for (int j = 0; j < 15 && kbd.layout[i][j].width; j++) {
      struct key *k = &kbd.layout[i][j];
      int total_width = keywidth(k);
      char *style = styles[i][j];
      if (!style) style = "\x1b[m";
      string_push_format_slow(&b, "%s%s%.*s%s", style, topleft, (int)(total_width * strlen(dash)), dashes, topright);
    }
    string_push_format_slow(&b, "\r\n");
    for (int j = 0; j < 15 && kbd.layout[i][j].width; j++) {
      struct key *k = &kbd.layout[i][j];
      int textwidth = utf8_strlen(k->text);
      int total_width = keywidth(k);
      int padding = total_width - textwidth;
      int leftpad = padding / 2;
      int rightpad = padding - leftpad;
      assert(padding >= 0);
      char *style = styles[i][j];
      if (!style) style = "\x1b[m";
      string_push_format_slow(&b, "%s%s%.*s%s%.*s%s\x1b[m", style, pipe, leftpad, spaces, k->text, rightpad, spaces, pipe);
    }
    string_push_format_slow(&b, "\r\n");
    for (int j = 0; j < 15 && kbd.layout[i][j].width; j++) {
      struct key *k = &kbd.layout[i][j];
      int total_width = keywidth(k);
      char *style = styles[i][j];
      if (!style) style = "\x1b[m";
      string_push_format_slow(&b, "%s%s%.*s%s", style, bottomleft, (int)(total_width * strlen(dash)), dashes, bottomright);
    }
    string_push_format_slow(&b, "\r\n");
  }
  io_write(STDOUT_FILENO, string_as_u8_slice(b));
}

bool quit = false;
void on_signal(int sig) {
  (void)sig;
  terminal_reset();
  quit = true;
  // exit(0);
}

void highlight_and_draw(struct velvet_keymap *k, struct velvet_key_event e) {
  (void)k;
  char *highlight = e.type == VELVET_API_KEY_EVENT_TYPE_RELEASE ? nullptr : "\x1b[7m";

  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < 15; j++) {
      struct key k = kbd.layout[i][j];
      if (!k.name) continue;
      if (e.key.kitty_terminator == 'u' && e.key.codepoint) {
        struct utf8 u = {0};
        int n = codepoint_to_utf8(e.key.codepoint, &u);
        int n2 = strlen(k.name);
        if (n == n2 && strcasecmp((char *)u.utf8, k.name) == 0) styles[i][j] = highlight;
      }

      if (k.name && e.key.name) {
        for (int idx = 0; idx < LENGTH(named_keys); idx++) {
          struct velvet_key k2 = named_keys[idx];
          if (!k2.name) continue;
          if (k2.codepoint != e.key.codepoint || e.key.kitty_terminator != k2.kitty_terminator) continue;
          if (strcasecmp(k2.name, k.name) == 0) {
            styles[i][j] = highlight;
            break;
          }
        }
      }
    }
  }
  draw_keyboard();
}

static void quit_keybind(struct velvet_keymap *k, struct velvet_key_event e) {
  (void)k;
  (void)e;
  quit = true;
}

int main(void) {
  signal(SIGTERM, on_signal);
  signal(SIGINT, on_signal);
  terminal_setup();

  struct velvet v = {
      .input = velvet_input_default,
      .event_loop = io_default,
      .scene = velvet_scene_default,
  };

  {
    struct velvet_keymap *root = calloc(1, sizeof(*root));
    *root = (struct velvet_keymap){
        .root = root,
        .data = &v,
        .on_key = highlight_and_draw,
    };
    v.input.keymap = root;
    struct velvet_keymap *quit = velvet_keymap_map(root, u8_slice_from_cstr("qqq"));
    quit->on_key = quit_keybind;
  }

  draw_keyboard();

  int n;
  uint8_t readbuf[100];
  while (!quit && (n = read(STDIN_FILENO, readbuf, 100)) != 0) {
    if (n < 0) continue;
    struct u8_slice s = { .content = readbuf, .len = n };
    velvet_input_process(&v, s);
  }
  terminal_reset();
}
