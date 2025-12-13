#include <stdio.h>
#include "text.h"
#include "utils.h"
#include <string.h>

struct key {
  char *text;
  float width;
};

struct keyboard {
  struct key layout[6][15];
};

#define K3(t){.text = t, .width = 1.0f}
#define K2(t, w){.text = #t, .width = w}
#define K(t) K2(t, 1.0f)

static int keywidth(struct key *k) {
  return (int)(k->width * 8) - 2;
}

int main(void) {
  char *spaces = "                                                                                                           ";
  char *dashes = "───────────────────────────────────────────────────────────────────────────────────────────────────────────";
  char *topleft = "┌";
  char *topright = "┐";
  char *bottomleft = "└";
  char *bottomright = "┘";
  char *pipe = "│";
  char *dash = "─";
  // char *top_connector = "┬";
  // char *left_connector = "├";
  // char *right_connector = "┤";
  // char *cross_connector = "┼";

  struct keyboard kbd = {{
    {K2(ESC, 1.5), K(F1), K(F2), K(F3), K(F4), K(F5), K(F6), K(F7), K(F8), K(F9), K(F10), K(F11), K(F12), K(DEL)},
    {K3("§"), K(1), K(2), K(3), K(4), K(5), K(6), K(7), K(8), K(9), K(0), K(-), K(=), K2(BS, 1.5)},
    {K2(TAB, 1.5), K(Q), K(W), K(E), K(R), K(T), K(Y), K(U), K(I), K(O), K(P), K({), K(}), K2(\\, 1)},
    {K2(CAPS, 2.0), K(A), K(S), K(D), K(F), K(G), K(H), K(J), K(K), K(L), K(;), K3("'"), K2(RET, 1.5) },
    {K2(LSFT, 1.5), K(~), K(Z), K(X), K(C), K(V), K(B), K(N), K(M), K3(","), K(.), K(/), K3("↑"), K2(RSFT, 1) },
    {K(FN), K(CTRL), K(LOPT), K2(CMD, 1.25), K2(SPACE, 5), K2(CMD, 1.25), K(ROPT), K3("←"), K3("↓"), K3("→") },
  }};

  char *styles[6][15] = {
      [2] =
          {
              [5] = "\x1b[33m",
          },
  };

  int i = 0;
  for (; i < 6; i++) {
    for (int j = 0; j < 15 && kbd.layout[i][j].width; j++) {
      struct key *k = &kbd.layout[i][j];
      int total_width = keywidth(k);
      char *style = styles[i][j];
      if (!style) style = "\x1b[m";
      printf("%s%s%.*s%s", style, topleft, (int)(total_width * strlen(dash)), dashes, topright);
    }
    printf("\n");
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
      printf("%s%s%.*s%s%.*s%s", style, pipe, leftpad, spaces, k->text, rightpad, spaces, pipe);
    }
    printf("\n");
    for (int j = 0; j < 15 && kbd.layout[i][j].width; j++) {
      struct key *k = &kbd.layout[i][j];
      int total_width = keywidth(k);
      char *style = styles[i][j];
      if (!style) style = "\x1b[m";
      printf("%s%s%.*s%s", style, bottomleft, (int)(total_width * strlen(dash)), dashes, bottomright);
    }
    printf("\n");
  }
}
