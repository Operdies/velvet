#include "screen.h"
#include "utils.h"

#define HEX_TO_NUM(x) (((x) >= '0' && (x) <= '9') ? (x) - '0' : (x) - 'a' + 10)
#define RGB(rgb)                                                                                                       \
  (struct color) {                                                                                                     \
    .cmd = COLOR_RGB, .r = (HEX_TO_NUM(rgb[1]) << 4) | (HEX_TO_NUM(rgb[2])),                                           \
    .g = (HEX_TO_NUM(rgb[3]) << 4) | (HEX_TO_NUM(rgb[4])), .b = (HEX_TO_NUM(rgb[5]) << 4) | (HEX_TO_NUM(rgb[5]))       \
  }

/* catppuccin mocha palette */
static const struct color ansi16[16] = {
    /* 30-37 */
    RGB("#45475a"),
    RGB("#f38ba8"),
    RGB("#a6e3a1"),
    RGB("#f9e2af"),
    RGB("#89b4fa"),
    RGB("#f5c2e7"),
    RGB("#94e2d5"),
    RGB("#bac2de"),
    /* 90-97 */
    RGB("#585b70"),
    RGB("#f38ba8"),
    RGB("#a6e3a1"),
    RGB("#f9e2af"),
    RGB("#89b4fa"),
    RGB("#f5c2e7"),
    RGB("#94e2d5"),
    RGB("#a6adc8"),
};

/* XTERM palette */
// static const struct color ansi16[16] = {
//     /* 30–37 */
//     RGB("#000000"), // black
//     RGB("#cd0000"), // red
//     RGB("#00cd00"), // // green
//     RGB("#cdcd00"), // yellow
//     RGB("#0000ee"), // blue
//     RGB("#cd00cd"), // magenta
//     RGB("#00cdcd"), // cyan
//     RGB("#e5e5e5"), // white (light gray)
//     /* 90–97 */
//     RGB("#7f7f7f"), // bright black (dark gray)
//     RGB("#ff0000"), // bright red
//     RGB("#00ff00"), // bright green
//     RGB("#ffff00"), // bright yellow
//     RGB("#5c5cff"), // bright blue
//     RGB("#ff00ff"), // bright magenta
//     RGB("#00ffff"), // bright cyan
//     RGB("#ffffff"), // bright white
// };

static struct color xterm256_to_rgb(uint8_t n) {
  /* ANSI 16 colors */
  if (n < 16) return ansi16[n];

  /* Color cube */
  if (n >= 16 && n <= 231) {
    n -= 16;
    int r = n / 36;
    int g = (n / 6) % 6;
    int b = n % 6;

    static const uint8_t levels[6] = {0, 95, 135, 175, 215, 255};

    return (struct color){.cmd = COLOR_RGB, .r = levels[r], .g = levels[g], .b = levels[b]};
  }

  /* Grayscale ramp */
  if (n >= 232) {
    uint8_t v = 8 + (n - 232) * 10;
    return (struct color){.cmd = COLOR_RGB, .r = v, .g = v, .b = v};
  }

  return RGB("#000000");
}

struct color velvet_composite_normalize_colors(struct color c) {
  if (c.cmd == COLOR_TABLE) return xterm256_to_rgb(c.table);
  return c;
}

struct color velvet_composite_color_amplify(struct color col, float magnitude) {
  col = velvet_composite_normalize_colors(col);
  col.r *= magnitude;
  col.g *= magnitude;
  col.b *= magnitude;
  col.r = CLAMP(col.r, 0, 255);
  col.g = CLAMP(col.g, 0, 255);
  col.b = CLAMP(col.b, 0, 255);
  return col;
}

struct color velvet_composite_color_blend(struct color l, struct color r, float opacity) {
  assert(opacity >= 0 && opacity <= 1.0f);
  float hi = opacity;
  float lo = 1.0f - opacity;
  l = velvet_composite_normalize_colors(l);
  r = velvet_composite_normalize_colors(r);

  float red1 = ((float)l.r) * hi;
  float red2 = ((float)r.r) * lo;
  float blue1 = ((float)l.b) * hi;
  float blue2 = ((float)r.b) * lo;
  float green1 = ((float)l.g) * hi;
  float green2 = ((float)r.g) * lo;

  struct color out = {
      .cmd = COLOR_RGB,
      .r = red1 + red2,
      .g = green1 + green2,
      .b = blue1 + blue2,
  };
  return out;
}
