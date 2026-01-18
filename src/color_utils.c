/* chatgpt ass file, but I'm not that into colors */
#include <math.h>
#include <stdint.h>

static float clampf(float x, float min, float max) {
  if (x < min) return min;
  if (x > max) return max;
  return x;
}

/* Convert RGB (0–1) to HSV (H: 0–360, S/V: 0–1) */
static void rgb_to_hsv(float r, float g, float b, float *h, float *s, float *v) {
  float max = fmaxf(r, fmaxf(g, b));
  float min = fminf(r, fminf(g, b));
  float delta = max - min;

  *v = max;

  if (max == 0.0f) {
    *s = 0.0f;
    *h = 0.0f;
    return;
  }

  *s = delta / max;

  if (delta == 0.0f) {
    *h = 0.0f;
  } else if (max == r) {
    *h = 60.0f * fmodf(((g - b) / delta), 6.0f);
  } else if (max == g) {
    *h = 60.0f * (((b - r) / delta) + 2.0f);
  } else {
    *h = 60.0f * (((r - g) / delta) + 4.0f);
  }

  if (*h < 0.0f) *h += 360.0f;
}

/* Convert HSV to RGB (all in range 0–1 except H) */
static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b) {
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;

  float rp, gp, bp;

  if (h < 60.0f) {
    rp = c;
    gp = x;
    bp = 0;
  } else if (h < 120.0f) {
    rp = x;
    gp = c;
    bp = 0;
  } else if (h < 180.0f) {
    rp = 0;
    gp = c;
    bp = x;
  } else if (h < 240.0f) {
    rp = 0;
    gp = x;
    bp = c;
  } else if (h < 300.0f) {
    rp = x;
    gp = 0;
    bp = c;
  } else {
    rp = c;
    gp = 0;
    bp = x;
  }

  *r = rp + m;
  *g = gp + m;
  *b = bp + m;
}

/* Dim an sRGB color using HSV value scaling */
void dim_rgb_hsv(uint8_t r, uint8_t g, uint8_t b, float factor, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
  /* Normalize RGB */
  float fr = r / 255.0f;
  float fg = g / 255.0f;
  float fb = b / 255.0f;

  /* Convert to HSV */
  float h, s, v;
  rgb_to_hsv(fr, fg, fb, &h, &s, &v);

  /* Dim value */
  v = clampf(v * factor, 0.0f, 1.0f);

  /* Convert back to RGB */
  hsv_to_rgb(h, s, v, &fr, &fg, &fb);

  /* Convert back to 8-bit */
  *out_r = (uint8_t)roundf(clampf(fr, 0.0f, 1.0f) * 255.0f);
  *out_g = (uint8_t)roundf(clampf(fg, 0.0f, 1.0f) * 255.0f);
  *out_b = (uint8_t)roundf(clampf(fb, 0.0f, 1.0f) * 255.0f);
}

static uint8_t srgb_to_linear(float c) {
  if (c <= 0.04045f)
    c = c / 12.92f;
  else
    c = powf((c + 0.055f) / 1.055f, 2.4f);
  return c * 255;
}

static float linear_to_srgb(uint8_t col) {
  float c = (float)col / 255;
  if (c <= 0.0031308f)
    c =  12.92f * c;
  else
    c =  1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
  return c;
}

