#include <stdio.h>
#include "text.h"
#include "utils.h"
#include "velvet.h"
#include <string.h>

struct key {
  char *text;
  float width;
  char *name;
};

struct keyboard {
  struct key layout[6][15];
};

#define K5(t, w, n){.text = #t, .width = w, .name = #n}
#define K4(t, n){.text = t, .width = 1.0f, .name = #n}
#define K3(t){.text = t, .width = 1.0f, .name = t}
#define K2(t, w){.text = #t, .width = w, .name = #t}
#define K(t) K2(t, 1.0f)

static int keywidth(struct key *k) {
  return (int)(k->width * 8) - 2;
}

static struct keyboard kbd = {{
  {K2(ESC, 1.5), K(F1), K(F2), K(F3), K(F4), K(F5), K(F6), K(F7), K(F8), K(F9), K(F10), K(F11), K(F12), K(DEL)},
  {K3("§"), K(1), K(2), K(3), K(4), K(5), K(6), K(7), K(8), K(9), K(0), K(-), K(=), K2(BS, 1.5)},
  {K2(TAB, 1.5), K(Q), K(W), K(E), K(R), K(T), K(Y), K(U), K(I), K(O), K(P), K([), K(]), K2(\\, 1)},
  {K2(CAPS, 2.0), K(A), K(S), K(D), K(F), K(G), K(H), K(J), K(K), K(L), K(;), K3("'"), K2(RET, 1.5) },
  {K2(LSFT, 1.5), K(`), K(Z), K(X), K(C), K(V), K(B), K(N), K(M), K3(","), K(.), K(/), K4("↑", UP), K2(RSFT, 1) },
  {K(FN), K(CTRL), K(LOPT), K2(CMD, 1.25), K2(SPACE, 5), K2(CMD, 1.25), K(ROPT), K4("←", LEFT), K4("↓", DOWN), K4("→", RIGHT) },
}};

static char *styles[6][15] = { 0 };

static void draw_keyboard() {
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
  printf("\x1b[m\x1b[H\x1b[2J");
  for (; i < 6; i++) {
    for (int j = 0; j < 15 && kbd.layout[i][j].width; j++) {
      struct key *k = &kbd.layout[i][j];
      int total_width = keywidth(k);
      char *style = styles[i][j];
      if (!style) style = "\x1b[m";
      printf("%s%s%.*s%s", style, topleft, (int)(total_width * strlen(dash)), dashes, topright);
    }
    printf("\r\n");
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
      printf("%s%s%.*s%s%.*s%s\x1b[m", style, pipe, leftpad, spaces, k->text, rightpad, spaces, pipe);
    }
    printf("\r\n");
    for (int j = 0; j < 15 && kbd.layout[i][j].width; j++) {
      struct key *k = &kbd.layout[i][j];
      int total_width = keywidth(k);
      char *style = styles[i][j];
      if (!style) style = "\x1b[m";
      printf("%s%s%.*s%s", style, bottomleft, (int)(total_width * strlen(dash)), dashes, bottomright);
    }
    printf("\r\n");
  }
}

bool quit = false;
void on_signal(int sig) {
  terminal_reset();
  quit = true;
  // exit(0);
}

void highlight_and_draw(struct velvet_keymap *k, struct velvet_key_event e) {
  if (!e.modifiers && e.key.codepoint == 'q') {
    quit = true; return;
  }
  char *highlight = "\x1b[42m";
  if (e.modifiers & MODIFIER_SHIFT) styles[4][0] = highlight;
  if (e.modifiers & MODIFIER_ALT) styles[5][2] = highlight;
  if (e.modifiers & MODIFIER_CTRL) styles[5][1] = highlight;
  if (e.modifiers & MODIFIER_SUPER) styles[5][3] = highlight;
  if (e.modifiers & MODIFIER_HYPER) styles[5][0] = highlight;
  if (e.modifiers & MODIFIER_META) styles[5][2] = highlight;
  if (e.modifiers & MODIFIER_CAPS_LOCK) styles[3][0] = highlight;

  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < 15; j++) {
      struct key k = kbd.layout[i][j];
      if (!k.name) continue;
      if (e.key.kitty_final == 'u' && e.key.codepoint) {
        struct utf8 u = {0};
        int n = codepoint_to_utf8(e.key.codepoint, &u);
        int n2 = strlen(k.name);
        if (n == n2 && strcasecmp((char *)u.utf8, k.name) == 0) styles[i][j] = highlight;
      }

      if (k.name && e.key.name) {
        for (int idx = 0; idx < LENGTH(named_keys); idx++) {
          struct velvet_key k2 = named_keys[idx];
          if (!k2.name) continue;
          if (k2.codepoint != e.key.codepoint || e.key.kitty_final != k2.kitty_final) continue;
          if (strcasecmp(k2.name, k.name) == 0) {
            styles[i][j] = highlight;
            break;
          }
        }
      }
    }
  }
  draw_keyboard();
  for (int i = 0; i < 6; i++)
    for (int j = 0; j < 15; j++)
      styles[i][j] = nullptr;
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
  }

  draw_keyboard();

  int n;
  uint8_t readbuf[100];
  while (!quit && (n = read(STDIN_FILENO, readbuf, 100)) != 0) {
    if (n < 0) continue;
    if (n == 1 && readbuf[0] == 'q') break;
    struct u8_slice s = { .content = readbuf, .len = n };
    velvet_input_process(&v, s);
  }
  terminal_reset();
}
