#include "utils.h"
#include "virtual_terminal_sequences.h"
#include "vte.h"
#include <string.h>
#include <sys/wait.h>
#include <velvet_scene.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>

static bool cell_equals(struct screen_cell a, struct screen_cell b);
static bool cell_style_equals(struct screen_cell_style a, struct screen_cell_style b);
static bool color_equals(struct color a, struct color b);

struct rect rect_carve(struct rect source, int x, int y, int width, int height) {
  int pixels_per_column = (int)((float)source.x_pixel / (float)source.w);
  int pixels_per_row = (int)((float)source.y_pixel / (float)source.h);

  struct rect out = { .x = x, .y = y, .w = width, .h = height };
  out.x_pixel = out.w * pixels_per_column;
  out.y_pixel = out.h * pixels_per_row;
  return out;
}

static int window_cmp(const void *a1, const void *b1) {
  const struct velvet_window *a = a1;
  const struct velvet_window *b = b1;

  /* use win->id as a tie breaker to ensure a stable sort. This avoids clipping if windows overlap on the same Z index. */
  return a->z_index == b->z_index ? a->id - b->id : a->z_index - b->z_index;
}

static void velvet_window_notify_focus(struct velvet_window *host, bool focus) {
  if (host->pty && host->emulator.options.focus_reporting) {
    string_push_slice(&host->emulator.pending_input, focus ? vt_focus_in : vt_focus_out);
  }
}

struct velvet_window *velvet_scene_get_window_from_id(struct velvet_scene *m, int id) {
  struct velvet_window *w;
  vec_find(w, m->windows, w->id == id);
  return w;
}

struct velvet_window *velvet_scene_get_focus(struct velvet_scene *m) {
  if (m->focus_order.length) {
    int *f = vec_nth(m->focus_order, 0);
    return velvet_scene_get_window_from_id(m, *f);
  }
  return nullptr;
}

static void focus_remove(struct velvet_scene *m, struct velvet_window *f) {
  int *foc;
  vec_find(foc, m->focus_order, *foc == f->id);
  if (foc) vec_remove(&m->focus_order, foc);
}

void velvet_scene_set_focus(struct velvet_scene *m, struct velvet_window *new_focus) {
  if (new_focus->hidden) return;
  struct velvet_window *current_focus = velvet_scene_get_focus(m);
  if (new_focus != current_focus) {
    focus_remove(m, new_focus);
    vec_insert(&m->focus_order, 0, &new_focus->id);
    /* TODO: Current focus not set when leaving workspace */
    if (current_focus) velvet_window_notify_focus(current_focus, false);
    velvet_window_notify_focus(new_focus, true);
  }
}

static int get_id() {
  static int id = 1000;
  return id++;
}

struct velvet_window * velvet_scene_manage(struct velvet_scene *m, struct velvet_window template) {
  assert(m->windows.element_size == sizeof(struct velvet_window));
  struct velvet_window *host = vec_new_element(&m->windows);
  *host = template;
  if (!host->id) host->id = get_id();

  vec_push(&m->focus_order, &host->id);
  host->events.data = m->events.data;
  host->events.on_move = m->events.on_window_moved;
  host->events.on_resize = m->events.on_window_resized;
  return host;
}

static void velvet_scene_remove_window(struct velvet_scene *m, struct velvet_window *w) {
  int win_id = w->id;
  int initial_focus = velvet_scene_get_focus(m)->id;
  ssize_t index = vec_index(&m->windows, w);
  assert(index >= 0);
  focus_remove(m, w);
  vec_remove(&m->windows, w);

  struct velvet_window *new_focus = velvet_scene_get_focus(m);
  if (new_focus && new_focus->id != initial_focus) velvet_window_notify_focus(new_focus, true);
  if (m->events.on_window_removed) 
    m->events.on_window_removed(win_id, m->events.data);
}

int velvet_scene_spawn_process_from_template(struct velvet_scene *scene, struct velvet_window template) {
  assert(scene->windows.element_size == sizeof(struct velvet_window));
  struct velvet_window *host = velvet_scene_manage(scene, template);
  bool started = velvet_window_start(host);
  if (!started) {
    velvet_scene_remove_window(scene, host);
    return 0;
  }
  if (scene->events.on_window_created) 
    scene->events.on_window_created(host->id, scene->events.data);
  return host->id;
}

int velvet_scene_spawn_process(struct velvet_scene *scene, struct u8_slice cmdline) {
  assert(scene->windows.element_size == sizeof(struct velvet_window));
  struct velvet_window host = { 0 };
  string_push_slice(&host.cmdline, cmdline);
  host.emulator = vte_default;
  host.border_width = 1;
  return velvet_scene_spawn_process_from_template(scene, host);
}

static void velvet_render_destroy(struct velvet_render *renderer) {
  string_destroy(&renderer->draw_buffer);
  for (int i = 0; i < LENGTH(renderer->buffers); i++) {
    free(renderer->buffers[i].cells);
    free(renderer->buffers[i].lines);
  }
  free(renderer->staging_buffer.cells);
  free(renderer->staging_buffer.lines);
}

void velvet_scene_resize(struct velvet_scene *m, struct rect w) {
  if (m->ws.w != w.w || m->ws.h != w.h || m->ws.x_pixel != w.x_pixel || m->ws.y_pixel != w.y_pixel) {
    m->ws = w;
    if (m->events.on_screen_resized) m->events.on_screen_resized(m->events.data);
  }
}

static struct color xterm256_to_rgb(struct velvet_theme t, uint8_t n) {
  /* ANSI 16 colors */
  if (n < 16) return t.palette[n];

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

static struct color color_to_rgb(struct velvet_theme t, struct color c, bool fg) {
  if (c.cmd == COLOR_RESET) return fg ? t.foreground : t.background;
  if (c.cmd == COLOR_TABLE) return xterm256_to_rgb(t, c.table);
  return c;
}

static struct color rgb_mult(struct color a, float m) {
  assert(a.cmd == COLOR_RGB);
  a.r = CLAMP((float)a.r * m, 0, 255);
  a.g = CLAMP((float)a.g * m, 0, 255);
  a.b = CLAMP((float)a.b * m, 0, 255);
  return a;
}

static struct color rgb_add(struct color a, struct color b) {
  assert(a.cmd == COLOR_RGB);
  assert(b.cmd == COLOR_RGB);
  a.r = CLAMP(a.r + b.r, 0, 255);
  a.g = CLAMP(a.g + b.g, 0, 255);
  a.b = CLAMP(a.b + b.b, 0, 255);
  return a;
}

/* blend colors such that out == a * frac + b * (1.0f-frac) */
static struct color color_alpha_blend(struct color a, struct color b, float frac) {
  assert(a.cmd == COLOR_RGB);
  assert(b.cmd == COLOR_RGB);
  return rgb_add(rgb_mult(a, frac), rgb_mult(b, 1.0f - frac));
}


/* convert cell colors to RGB colors based on the current theme, and convert null characters to spaces */
static struct screen_cell normalize_cell(struct velvet_theme t, struct screen_cell c) {
  c.style.bg = color_to_rgb(t, c.style.bg, false);
  c.style.fg = color_to_rgb(t, c.style.fg, true);
  if (c.style.attr & ATTR_REVERSE) {
    c.style.attr &= ~ATTR_REVERSE;
    struct color fg = c.style.fg;
    c.style.fg = c.style.bg;
    c.style.bg = fg;
  }
  if (!c.cp.value) c.cp.value = ' ';
  return c;
}

static struct screen_cell *velvet_render_get_staged_cell(struct velvet_render *r, int line, int column) {
  if (!(line >= 0 && line < r->h)) return nullptr;
  if (!(column >= 0 && column < r->w)) return nullptr;

  struct velvet_render_buffer *b = &r->staging_buffer;
  struct velvet_render_buffer_line *l = &b->lines[line];
  return &l->cells[column];
}

static void velvet_render_set_cell(struct velvet_render *r, int line, int column, struct screen_cell value) {
  /* out of bounds writes here is not a bug. It is expected for controls which are partially off-screen. */
  if (!(line >= 0 && line < r->h)) return;
  if (!(column >= 0 && column < r->w)) return;
  struct velvet_render_buffer *b = &r->staging_buffer;
  struct velvet_render_buffer_line *l = &b->lines[line];
  struct screen_cell *c = l->cells;
  
  /* a wide char cannot start on the last column */
  if (value.cp.is_wide && column >= (r->w - 1)) 
    value.cp = codepoint_space;

  /* if the previous cell held a wide char, reset it. */
  if (column && c[column-1].cp.is_wide) 
    c[column-1].cp = codepoint_space;

  c[column] = value;
  if (value.cp.is_wide && column < (r->w - 1)) {
    /* if this cell is wide, reset the next cell if possible (including style override) */
    value.cp = codepoint_space;
    c[column + 1] = value;
  }
}

static void velvet_render_set_cursor_visible(struct velvet_render *r, bool visible) {
  if (r->state.cursor.options.visible != visible) {
    string_push_slice(&r->draw_buffer, visible ? vt_cursor_visible_on : vt_cursor_visible_off);
  }
  r->state.cursor.options.visible = visible;
}

static void velvet_render_set_cursor(struct velvet_render *r, struct cursor_options cursor) {
  if (r->state.cursor.options.style != cursor.style) {
    string_push_csi(&r->draw_buffer, 0, INT_SLICE(cursor.style), " q");
  }
  r->state.cursor.options.style = cursor.style;
  velvet_render_set_cursor_visible(r, cursor.visible);
}

static void velvet_render_set_style(struct velvet_render *r, struct screen_cell_style style);

static void velvet_render_position_cursor(struct velvet_render *r, int line, int col) {
  line = CLAMP(line, 0, r->h - 1);
  col = CLAMP(col, 0, r->w - 1);
  if (r->state.cursor.position.line != line && r->state.cursor.position.column != col) {
    string_push_csi(&r->draw_buffer, 0, INT_SLICE(line + 1, col + 1), "H");
  } else if (r->state.cursor.position.line != line) {
    string_push_csi(&r->draw_buffer, 0, INT_SLICE(line + 1), "d");
  } else if (r->state.cursor.position.column != col) {
    string_push_csi(&r->draw_buffer, 0, INT_SLICE(col + 1), "G");
  }

  r->state.cursor.position.column = col;
  r->state.cursor.position.line = line;
}

static void render_buffer_add_damage(struct velvet_render_buffer_line *f, int start, int end, bool consolidate) {
  /* merge with previous damage if they are reasonably close. The reason being that in most cases, it is cheaper to emit
   * a few characters than a cursor move. Consolidation is disabled when `display_damage` is set. */
  static const int consolidate_max = 10;
  int n_damage = f->n_damage;

  if (consolidate && n_damage && start - f->damage[n_damage - 1].end < consolidate_max) {
    f->damage[n_damage - 1].end = end;
  } else {
    f->damage[n_damage].start = start;
    f->damage[n_damage].end = end;
    f->n_damage++;
  }
}

static struct velvet_render_buffer *get_previous_buffer(struct velvet_render *r) {
  int mod = r->options.display_damage ? 4 : 2;
  return &r->buffers[(r->current_buffer + LENGTH(r->buffers) - 1) % mod];
}

static struct velvet_render_buffer *get_current_buffer(struct velvet_render *r) {
  return &r->buffers[r->current_buffer];
}

static int velvet_render_calculate_damage(struct velvet_render *r) {
  int damage = 0;
  struct velvet_render_buffer *front = get_current_buffer(r);
  struct velvet_render_buffer *back = get_previous_buffer(r);

  for (int line = 0; line < r->h; line++) {
    struct velvet_render_buffer_line *f = &front->lines[line];
    struct velvet_render_buffer_line *b = &back->lines[line];

    f->n_damage = 0;
    int start = 0;
    for (; f->n_damage < DAMAGE_MAX - 1 && start < r->w; start++) {
      if (!cell_equals(f->cells[start], b->cells[start])) {
        int end = start + 1;
        for (; end < r->w && !cell_equals(f->cells[end], b->cells[end]); end++);
        end--;
        assert(start <= end);
        assert(end < r->w);
        render_buffer_add_damage(f, start, end, !r->options.display_damage);
        start = end;
        if (end == r->w - 1) break;
      }
    }

    for (; start < r->w; start++) {
      if (!cell_equals(f->cells[start], b->cells[start])) {
        for (int end = r->w - 1; end >= start; end--) {
          if (!cell_equals(f->cells[end], b->cells[end])) {
            assert(start <= end);
            assert(end < r->w);
            render_buffer_add_damage(f, start, end, !r->options.display_damage);
            goto next;
          }
        }
      }
    }

  next:
    for (int i = 0; i < f->n_damage; i++)
      damage += 1 + (f->damage[i].end - f->damage[i].start);
  }
  return damage;
}

static void velvet_render_render_buffer(struct velvet_render *r,
                                        struct velvet_render_buffer *front,
                                        bool highlight_damage,
                                        struct screen_cell_style highlight) {
  for (int line = 0; line < r->h; line++) {
    struct velvet_render_buffer_line *f = &front->lines[line];
    for (int dmg = 0; dmg < f->n_damage; dmg++) {
      int start = f->damage[dmg].start;
      int end = f->damage[dmg].end;
      velvet_render_position_cursor(r, line, start);
      for (int col = start; col <= end; col++) {
        struct screen_cell *c = &f->cells[col];
        struct screen_cell_style cell_style = c->style;
        if (highlight_damage) {
          cell_style = highlight;
        }
        velvet_render_set_style(r, cell_style);

        struct utf8 sym;
        uint8_t utf8_len = codepoint_to_utf8(c->cp.value, &sym);
        struct u8_slice text = {.content = sym.utf8, .len = utf8_len};
        string_push_slice(&r->draw_buffer, text);

        bool wide = c->cp.is_wide;
        int stride = wide ? 2 : 1;
        int repeats = 1;
        int remaining = end - col - 1;
        for (; repeats * stride < remaining && cell_equals(c[0], c[repeats * stride]); repeats++);
        repeats--;
        if (repeats > 0) {
          int num_bytes = utf8_len * repeats;
          bool can_repeat = utf8_len == 1 || r->options.no_repeat_multibyte_symbols == false;
          if (num_bytes > 10 && can_repeat) {
            string_push_csi(&r->draw_buffer, 0, INT_SLICE(repeats), "b");
          } else {
            for (int i = 0; i < repeats; i++) {
              string_push_slice(&r->draw_buffer, text);
            }
          }
        }
        col += repeats * stride;
        if (wide) col++;
        r->state.cursor.position.column = MIN(col + 1, r->w - 1);
      }
    }
  }
}

static void velvet_render_render_damage_to_buffer(struct velvet_render *r) {
  /* clear should not be used, so let's make misuse obvious */
  struct screen_cell_style clear = {.fg = RGB("#ff00ff"), .bg = RGB("#00ffff") };
  if (r->options.display_damage) {
    struct color colors[] = {
        RGB("#f38ba8"),
        RGB("#fab387"),
        RGB("#a6e3a1"),
    };

    /* repair oldest damage (remove highlights) */
    struct velvet_render_buffer *oldest = &r->buffers[(r->current_buffer + 1) % LENGTH(r->buffers)];
    velvet_render_render_buffer(r, oldest, false, clear);

    /* Draw progressive damage by frame. The frame we are currently drawing will be colors[0], and so on. */
    for (int i = LENGTH(r->buffers) - 2; i >= 0; i--) {
      struct color c = colors[i];
      struct screen_cell_style s = {.bg = c, .fg = RGB("#313244")};
      struct velvet_render_buffer *it = &r->buffers[(LENGTH(r->buffers) + r->current_buffer - i) % LENGTH(r->buffers)];
      velvet_render_render_buffer(r, it, true, s);
    }
  } else {
    velvet_render_render_buffer(r, get_current_buffer(r), false, clear);
  }
}

static bool should_emulate_cursor(struct cursor_options cur) {
  if (!cur.visible) return false;
  switch (cur.style) {
  case CURSOR_STYLE_DEFAULT:
  case CURSOR_STYLE_BLINKING_BLOCK:
  case CURSOR_STYLE_STEADY_BLOCK:
  case CURSOR_STYLE_BLINKING_UNDERLINE:
  case CURSOR_STYLE_STEADY_UNDERLINE: return true;
  default: return false;
  }
}

static void velvet_render_copy_cells_from_window(struct velvet_scene *m, struct velvet_window *h, struct velvet_theme t) {
  struct velvet_render *r = &m->renderer;
  struct screen *active = vte_get_current_screen(&h->emulator);
  assert(active);
  assert(active->w == h->rect.client.w);
  assert(active->h == h->rect.client.h);

  for (int line = 0; line < active->h; line++) {
    struct screen_line *screen_line = screen_get_view_line(active, line);
    int render_line = h->rect.client.y + line;
    if (render_line >= r->h) break;
    for (int column = 0; column < active->w; column++) {
      int render_column = h->rect.client.x + column;
      if (render_column >= r->w) break;
      struct screen_cell cell = screen_line->cells[column];
      if (r->options.display_eol) {
        if (screen_line->has_newline) {
          if (screen_line->eol == 0 && column == 0) {
            cell.style.bg = RGB("#ff00ff");
          } else if (column == screen_line->eol - 1) {
            cell.style.bg = RGB("#ffff00");
          }
        } else if (column == screen_line->eol - 1) {
          cell.style.bg = RGB("#00ffff");
        }
      }

      velvet_render_set_cell(r, render_line, render_column, cell);
      if (cell.cp.is_wide) column++;
    }
  }

  bool is_focused = h == velvet_scene_get_focus(m);
  if (is_focused && should_emulate_cursor(h->emulator.options.cursor)) {
    int x = h->rect.client.x + active->cursor.column;
    int y = h->rect.client.y + active->cursor.line + screen_get_scroll_offset(active);
    if (y < h->rect.client.y + h->rect.client.h) {
      struct screen_cell *current = velvet_render_get_staged_cell(r, y, x);
      if (current) {
        struct screen_cell cursor = *current;
        switch (h->emulator.options.cursor.style) {
        case CURSOR_STYLE_DEFAULT:
        case CURSOR_STYLE_BLINKING_BLOCK:
        case CURSOR_STYLE_STEADY_BLOCK:
          cursor.style.fg = t.cursor.foreground;
          cursor.style.bg = t.cursor.background;
          break;
        case CURSOR_STYLE_BLINKING_UNDERLINE:
        case CURSOR_STYLE_STEADY_UNDERLINE: 
            cursor.style.attr |= ATTR_UNDERLINE; 
            break;
        default: break;
        };
        velvet_render_set_cell(r, y, x, cursor);
      }
    }
  }
}

/* lol very unsafe :) */
static struct codepoint codepoint_from_cstr(char *src) {
  struct utf8 result = {0};
  char *dst = (char *)result.utf8;
  for (; *src; *dst++ = *src++);

  int len;
  return utf8_to_codepoint(result.utf8, &len);
}

static void velvet_render_calculate_borders(struct velvet_scene *m, struct velvet_window *host) {
  static const int hard = 0;
  // static const int rounded = 1;
  // static const int dragging = 2;
  int style = hard;
  char *borders[3][4] = {
      {"┌", "┘", "┐", "└"},
      {"╭", "╯", "╮", "╰"},
      {"╔", "╝", "╗", "╚"},
  };

  struct velvet_render *r = &m->renderer;

  struct codepoint topleft = codepoint_from_cstr(borders[style][0]);
  struct codepoint bottomright = codepoint_from_cstr(borders[style][1]);
  struct codepoint topright = codepoint_from_cstr(borders[style][2]);
  struct codepoint bottomleft = codepoint_from_cstr(borders[style][3]);

  struct codepoint pipe = codepoint_from_cstr("│");
  struct codepoint dash = codepoint_from_cstr("─");
  // struct utf8 top_connector = utf8_from_cstr("┬");
  // struct utf8 left_connector = utf8_from_cstr("├");
  // struct utf8 right_connector = utf8_from_cstr("┤");
  // struct utf8 cross_connector = utf8_from_cstr("┼");
  struct codepoint elipsis = codepoint_from_cstr("…");
  bool is_focused = host == velvet_scene_get_focus(m);
  struct rect w = host->rect.window;
  struct rect c = host->rect.client;
  int bw = host->border_width;

  if (!bw) return;

  struct velvet_theme theme = m->theme;
  struct color chrome_color = is_focused ? theme.title.active : theme.title.inactive;
  struct screen_cell_style chrome = { .fg = chrome_color, .bg = theme.background };
  {
    struct screen_cell vert = {.cp = dash, .style = chrome};
    struct screen_cell horz = {.cp = pipe, .style = chrome};

    /* title chrome */
    for (int column = w.x + bw; column < w.x + w.w - bw; column++) velvet_render_set_cell(r, c.y - 1, column, vert);

    /* bottom chrome */
    for (int column = w.x + bw; column < w.x + w.w - bw; column++) velvet_render_set_cell(r, c.y + c.h, column, vert);

    /* left chrome */
    for (int line = c.y; line < c.y + c.h; line++) velvet_render_set_cell(r, line, c.x - 1, horz);

    /* right chrome */
    for (int line = c.y; line < c.y + c.h; line++) velvet_render_set_cell(r, line, c.x + c.w, horz);

    struct screen_cell corner = {.style = chrome };
    corner.cp = topleft;
    velvet_render_set_cell(r, c.y - 1, c.x - 1, corner);
    corner.cp = topright;
    velvet_render_set_cell(r, c.y - 1, c.x + c.w, corner);
    corner.cp = bottomleft;
    velvet_render_set_cell(r, c.y + c.h, c.x - 1, corner);
    corner.cp = bottomright;
    velvet_render_set_cell(r, c.y + c.h, c.x + c.w, corner);
  }

  {
    struct screen_cell truncation_symbol = {.cp = elipsis, .style = chrome};

    int title_end = c.x + c.w;

    /* draw scroll offset */
    struct screen *active = vte_get_current_screen(&host->emulator);
    if (screen_get_scroll_offset(active)) {
      int scroll_height = screen_get_scroll_height(active);
      char buf[40] = {0};
      struct string tmp = { .content = (uint8_t*)buf, .len = 0, .cap = sizeof(buf) };
      string_push_char(&tmp, '[');
      string_push_int(&tmp, screen_get_scroll_offset(active));
      string_push_char(&tmp, '/');
      string_push_int(&tmp, scroll_height);
      string_push_char(&tmp, ']');
      struct u8_slice_codepoint_iterator it = {.src = string_as_u8_slice(tmp)};
      int start = c.x + c.w - tmp.len;
      if (start > c.x) {
        while (u8_slice_codepoint_iterator_next(&it)) {
          struct screen_cell chr = {.style = chrome, .cp = it.current};
          velvet_render_set_cell(r, c.y - 1, start++, chr);
        }
      }
      title_end -= tmp.len;
    }

    int i = c.x + 1;
    /* draw the title */
    struct rect title_box = { .x = i, .h = 1, .y = c.y - 1 };
    struct u8_slice_codepoint_iterator it = {.src = string_as_u8_slice(host->title)};
    for (; i < title_end - 2 && u8_slice_codepoint_iterator_next(&it); i++) {
      struct screen_cell chr = {.style = chrome, .cp = it.current};
      velvet_render_set_cell(r, c.y - 1, i, chr);
    }

    /* add a space or truncation symbol */
    if (i < title_end - 1 && u8_slice_codepoint_iterator_next(&it)) {
      /* title was truncated */
      velvet_render_set_cell(r, c.y - 1, i, truncation_symbol);
      i++;
    }
    title_box.w = i - title_box.x;
    host->rect.title = title_box;
  }
}

static void velvet_render_init_buffer(struct velvet_render_buffer *buf, int w, int h) {
  free(buf->cells);
  free(buf->lines);
  buf->cells = velvet_calloc(w * h, sizeof(*buf->cells));
  buf->lines = velvet_calloc(h, sizeof(*buf->lines));
  for (int i = 0; i < h; i++) {
    buf->lines[i].cells = &buf->cells[i * w];
  }
}

static void velvet_render_init_buffers(struct velvet_scene *m) {
  struct velvet_render *r = &m->renderer;
  r->h = m->ws.h;
  r->w = m->ws.w;
  for (int i = 0; i < LENGTH(r->buffers); i++) velvet_render_init_buffer(&r->buffers[i], r->w, r->h);
  velvet_render_init_buffer(&r->staging_buffer, r->w, r->h);
  r->state = render_state_cache_invalidated;
}

static void velvet_render_clear_buffer(struct velvet_render *r, struct velvet_render_buffer *b, struct screen_cell space) {
  for (int i = 0; i < r->h * r->w; i++) {
    b->cells[i] = space;
  }
}

static void velvet_render_cycle_buffer(struct velvet_render *r) {
  int mod = r->options.display_damage ? 4 : 2;
  r->current_buffer = (r->current_buffer + 1) % mod;
}

void velvet_scene_set_display_damage(struct velvet_scene *m, bool display_damage) {
  if (m->renderer.options.display_damage != display_damage) {
    if (!display_damage) {
        /* fully wipe out buffers -- unlike swap_buffers, this actually zero's cells instead of turning them into
         * spaces. This is needed because highlighted spaces will not register as damage */
        for (int i = 0; i < LENGTH(m->renderer.buffers); i++) {
          memset(m->renderer.buffers[i].cells, 0, sizeof(struct screen_cell) * m->renderer.w * m->renderer.h);
        }
    }
    m->renderer.options.display_damage = display_damage;
  }
}

void velvet_scene_render_full(struct velvet_scene *m, render_func_t *render_func, void *context) {
  if (m->windows.length == 0) return;
  if (!m->renderer.buffers[0].cells) return;

  /* fully damage buffers */
  for (int i = 0; i < LENGTH(m->renderer.buffers); i++) {
    memset(m->renderer.buffers[i].cells, 0, sizeof(struct screen_cell) * m->renderer.w * m->renderer.h);
  }
  m->renderer.state = render_state_cache_invalidated;
  velvet_scene_render_damage(m, render_func, context);
}

struct composite_options {
  struct pseudotransparency_options transparency;
  float dim;
};

static bool is_cell_bg_clear(struct screen_cell c) {
  if (c.style.attr & ATTR_REVERSE) return c.style.fg.cmd == COLOR_RESET;
  else return c.style.bg.cmd == COLOR_RESET;
}

static void velvet_scene_commit_staged(struct velvet_scene *m, struct velvet_window *win, struct velvet_theme t) {
  bool is_focused = velvet_scene_get_focus(m) == win;
  struct composite_options o = {
      .transparency = win->transparency,
      .dim = !is_focused && t.dim_inactive.enabled ? t.dim_inactive.magnitude : 0,
  };

  struct screen_cell empty = {0};
  struct velvet_render *r = &m->renderer;
  struct velvet_render_buffer *composite = get_current_buffer(r);
  struct velvet_render_buffer *staging = &r->staging_buffer;
  for (int i = 0; i < r->w * r->h; i++) {
    if (!cell_equals(staging->cells[i], empty)) {
      struct screen_cell above = staging->cells[i];
      struct screen_cell below = composite->cells[i];
      int column = i % r->w;

      bool blend = o.transparency.mode != PSEUDOTRANSPARENCY_OFF &&
                   (o.transparency.mode == PSEUDOTRANSPARENCY_ALL || is_cell_bg_clear(above));

      if (blend) {
        above = normalize_cell(t, above);
        below = normalize_cell(t, below);
        struct screen_cell *before = column ? &composite->cells[i-1] : nullptr;
        bool is_wide_continuation = before && before->cp.is_wide && above.cp.value == ' ';

        /* if the top cell is blank, draw the glyph from the cell below, but tint it
         * with the background color of the top cell. This creates the illusion of transparency. */
        bool attributes_visible = above.style.attr & (ATTR_UNDERLINE_ANY | ATTR_FRAMED | ATTR_OVERLINED | ATTR_ENCIRCLED | ATTR_CROSSED_OUT);
        bool blend_fg = !attributes_visible && above.cp.value == ' ' && !is_wide_continuation;
        if (blend_fg) {
          above.cp = below.cp;
          above.style.attr = below.style.attr;
          above.style.fg = color_alpha_blend(below.style.fg, above.style.bg, o.transparency.alpha);
        }
        above.style.bg = color_alpha_blend(below.style.bg, above.style.bg, o.transparency.alpha);
      }

      if (o.dim > 0) {
        above = normalize_cell(t, above);
        above.style.bg = rgb_mult(above.style.bg, o.dim);
        above.style.fg = rgb_mult(above.style.fg, o.dim);
      }

      /* Wide chars on layers below can 'bleed through'. Clear the previous cell if it contains a wide char,
       * and this character is not a space. */
      if (above.cp.value != ' ' && column && composite->cells[i - 1].cp.is_wide) 
        composite->cells[i - 1].cp = codepoint_space;

      composite->cells[i] = above;
      staging->cells[i] = empty;
    }
  }
}

static bool rect_contains(struct rect r, int x, int y) {
  return r.x <= x && x < r.x + r.w && r.y <= y && y < r.y + r.h;
}

bool velvet_scene_hit(struct velvet_scene *scene, int x, int y, struct velvet_window_hit *hit, bool skip(struct velvet_window*, void*), void *data) {
  struct velvet_window *h;
  vec_sort(&scene->windows, window_cmp);
  vec_where(h, scene->windows, !h->hidden && (!skip || !skip(h, data))) {
    if (rect_contains(h->rect.client, x, y)) {
      struct velvet_window_hit client_hit = {.win = h, .where = VELVET_HIT_CLIENT};
      *hit = client_hit;
      return true;
    } else if (rect_contains(h->rect.window, x, y)) {
      struct rect w = h->rect.window;
      struct velvet_window_hit border_hit = {.win = h, .where = VELVET_HIT_BORDER};
      if (x == w.x) border_hit.where |= VELVET_HIT_BORDER_LEFT;
      if (x == w.x + w.w - 1) border_hit.where |= VELVET_HIT_BORDER_RIGHT;
      if (y == w.y) border_hit.where |= VELVET_HIT_BORDER_TOP;
      if (y == w.y + w.h - 1) border_hit.where |= VELVET_HIT_BORDER_BOTTOM;
      if (rect_contains(h->rect.title, x, y)) border_hit.where |= VELVET_HIT_TITLE;

      *hit = border_hit;
      return true;
    }
  }

  return false;
}

static void velvet_scene_stage_and_commit_window(struct velvet_scene *m, struct velvet_window *w) {
  struct velvet_theme t = m->theme;
  velvet_render_calculate_borders(m, w);
  if (w->emulator.options.reverse_video) {
    /* reverse video should not apply to borders, so we
     * need to commit twice if reverse video is used */
    velvet_scene_commit_staged(m, w, t);
    struct color fg = t.foreground;
    t.foreground = t.background;
    t.background = fg;

    fg = t.cursor.foreground;
    t.cursor.foreground = t.cursor.background;
    t.cursor.background = fg;
  }
  velvet_render_copy_cells_from_window(m, w, t);
  velvet_scene_commit_staged(m, w, t);
}

void velvet_scene_render_damage(struct velvet_scene *m, render_func_t *render_func, void *context) {
  if (m->windows.length == 0) return;
  vec_sort(&m->windows, window_cmp);

  struct velvet_render *r = &m->renderer;

  string_clear(&r->draw_buffer);
  if (r->h != m->ws.h || r->w != m->ws.w) {
    velvet_render_init_buffers(m);
    /* full clear (CSI 2J) causes flickering in some terminals
     * -- selectively erase everything outside of the draw region instead.
     * Note that DECERA (erase rectangle) exists, but it is not widely supported. */
    struct screen_cell_style clear = {.bg = m->theme.background};
    velvet_render_set_style(r, clear);
    struct u8_slice EL = u8_slice_from_cstr("\x1b[K");
    struct u8_slice ED = u8_slice_from_cstr("\x1b[J");
    /* 1. clear everything to the right of the draw area */
    for (int i = 0; i < r->h; i++) {
      velvet_render_position_cursor(r, i, r->w);
      string_push_slice(&r->draw_buffer, EL);
    }
    /* 2. Clear everything below the draw area */
    velvet_render_position_cursor(r, r->h, 0);
    string_push_slice(&r->draw_buffer, ED);
  } else {
    struct screen_cell space = {.cp = codepoint_space, .style.bg = m->theme.background};
    velvet_render_clear_buffer(r, get_current_buffer(r), space);
  }

  struct velvet_window *win;
  vec_foreach(win, m->windows) {
    velvet_window_update_title(win);
  }

  struct velvet_window *focused = velvet_scene_get_focus(m);

  vec_where(win, m->windows, !win->hidden) {
    velvet_scene_stage_and_commit_window(m, win);
  }

  int damage = velvet_render_calculate_damage(r);
  if (damage) {
    if (damage > 200) string_push_slice(&r->draw_buffer, vt_synchronized_rendering_on);
    velvet_render_render_damage_to_buffer(r);
    if (damage > 200) string_push_slice(&r->draw_buffer, vt_synchronized_rendering_off);
  }

  if (focused && !focused->hidden) {
    if (should_emulate_cursor(focused->emulator.options.cursor)) {
      velvet_render_set_cursor_visible(r, false);
    } else if (focused->emulator.options.cursor.visible) {
      struct screen *screen = vte_get_current_screen(&focused->emulator);
      struct cursor *cursor = &screen->cursor;
      int line = cursor->line + focused->rect.client.y + screen->scroll.view_offset;
      int col = cursor->column + focused->rect.client.x;

      bool cursor_obscured = false;
      /* if a window is above the current window and obscures the cursor, we should not show it */
      struct velvet_window_hit hit;
      if (velvet_scene_hit(m, col, line, &hit, nullptr, nullptr) && hit.win != focused) {
        if (hit.win->transparency.mode == PSEUDOTRANSPARENCY_OFF)
          cursor_obscured = true;
      }

      if (line < 0 || col < 0 || line >= m->ws.h || col >= m->ws.w) cursor_obscured = true;
      if (screen_get_scroll_offset(screen) + cursor->line >= screen->h) cursor_obscured = true;

      if (cursor_obscured) {
        /* hide the cursor */
        velvet_render_set_cursor_visible(r, false);
      } else {
        /* move cursor to focused host and update cursor */
        velvet_render_position_cursor(r, line, col);
        velvet_render_set_cursor(r, focused->emulator.options.cursor);
      }
    }
  } else {
    /* hide the cursor if nothing is focused. */
    velvet_render_set_cursor_visible(r, false);
  }

  struct u8_slice render = string_as_u8_slice(r->draw_buffer);
  render_func(render, context);
  if (damage) velvet_render_cycle_buffer(r);
}

struct sgr_param {
  uint8_t primary;
  uint8_t sub[4];
  int n_sub;
};

// Terminal emulators may not have infinite sapce set aside for SGR sequences,
// so let's split our sequences at this threshold. Such sequences should be unusual anyway.
#define MAX_LOAD 10
#define SGR_PARAMS_MAX 12
struct sgr_buffer {
  struct sgr_param params[SGR_PARAMS_MAX];
  uint8_t n;
};

static inline void sgr_buffer_push(struct sgr_buffer *b, int n) {
  assert(b->n < SGR_PARAMS_MAX);
  b->params[b->n] = (struct sgr_param){.primary = n};
  b->n++;
}
static inline void sgr_buffer_add_param(struct sgr_buffer *b, int sub) {
  assert(b->n);
  int k = b->n - 1;
  struct sgr_param *p = &b->params[k];
  p->sub[p->n_sub] = sub;
  p->n_sub++;
}

static void sgr_color_apply(struct sgr_buffer *sgr, struct color col, bool fg) {
  if (col.cmd == COLOR_RESET) {
    sgr_buffer_push(sgr, fg ? 39 : 49);
  } else if (col.cmd == COLOR_TABLE) {
    if (col.table <= 7) {
      sgr_buffer_push(sgr, (fg ? 30 : 40) + col.table);
    } else if (col.table <= 15) {
      sgr_buffer_push(sgr, (fg ? 90 : 100) + col.table - 8);
    } else {
      sgr_buffer_push(sgr, fg ? 38 : 48);
      sgr_buffer_add_param(sgr, 5);
      sgr_buffer_add_param(sgr, col.table);
    }
  } else if (col.cmd == COLOR_RGB) {
    sgr_buffer_push(sgr, fg ? 38 : 48);
    sgr_buffer_add_param(sgr, 2);
    sgr_buffer_add_param(sgr, col.r);
    sgr_buffer_add_param(sgr, col.g);
    sgr_buffer_add_param(sgr, col.b);
  }
}

static void velvet_render_set_style(struct velvet_render *r, struct screen_cell_style style) {
  struct color fg = r->state.cell.style.fg;
  struct color bg = r->state.cell.style.bg;
  uint32_t attr = r->state.cell.style.attr;
  struct sgr_buffer sgr = {.n = 0};

  // 1. Handle attributes
  if (attr != style.attr) {
    if (style.attr == 0) {
      // Unfortunately this also resets colors, so we need to set those again
      // Technically we could track what styles are active here and disable those specifically,
      // but it is much simpler to just eat the color reset.
      fg = bg = color_default;
      sgr_buffer_push(&sgr, 0);
    } else {
      uint32_t features[] = {
          [0] = ATTR_NONE,
          [1] = ATTR_BOLD,
          [2] = ATTR_FAINT,
          [3] = ATTR_ITALIC,
          [4] = ATTR_UNDERLINE,
          [5] = ATTR_BLINK_SLOW,
          [6] = ATTR_BLINK_RAPID,
          [7] = ATTR_REVERSE,
          [8] = ATTR_CONCEAL,
          [9] = ATTR_CROSSED_OUT,
      };
      for (size_t i = 1; i < LENGTH(features); i++) {
        uint32_t current = features[i] & attr;
        uint32_t next = features[i] & style.attr;
        if (current && !next) {
          if (i == 1) {
            // annoying special case for bold
            sgr_buffer_push(&sgr, 22);
          } else {
            sgr_buffer_push(&sgr, 20 + i);
          }
        } else if (!current && next) {
          sgr_buffer_push(&sgr, i);
        }
      }
    }
  }

  if (!color_equals(fg, style.fg)) {
    sgr_color_apply(&sgr, style.fg, true);
  }
  if (!color_equals(bg, style.bg)) {
    sgr_color_apply(&sgr, style.bg, false);
  }

  r->state.cell.style = style;

  if (sgr.n) {
    struct string *w = &r->draw_buffer;
    string_push(w, u8"\x1b[");
    int current_load = 0;
    for (int i = 0; i < sgr.n; i++) {
      struct sgr_param *p = &sgr.params[i];
      int this_load = 1 + p->n_sub;
      if (current_load + this_load > MAX_LOAD) {
        w->content[w->len - 1] = 'm';
        string_push(w, u8"\x1b[");
        current_load = 0;
      }
      current_load += this_load;
      string_push_int(w, p->primary);
      for (int j = 0; j < p->n_sub; j++) {
        // I would prefer to properly use ':' to split subparameters here, but it appears that
        // some terminal emulators do not properly implement this. For compatibility,
        // use ';' as a separator instead.
        string_push_char(w, ';');
        string_push_int(w, p->sub[j]);
      }
      string_push_char(w, ';');
    }
    w->content[w->len - 1] = 'm';
  }
}

static bool color_equals(struct color a, struct color b) {
  if (a.cmd != b.cmd) return false;
  switch (a.cmd) {
  case COLOR_RESET: return true;
  case COLOR_RGB: return a.r == b.r && a.g == b.g && a.b == b.b;
  case COLOR_TABLE: return a.table == b.table;
  }
  return false;
}

static bool cell_equals(struct screen_cell a, struct screen_cell b) {
  return a.cp.value == b.cp.value && cell_style_equals(a.style, b.style);
}

static bool cell_style_equals(struct screen_cell_style a, struct screen_cell_style b) {
  return a.attr == b.attr && color_equals(a.fg, b.fg) && color_equals(a.bg, b.bg);
}

extern pid_t forkpty(int *, char *, struct termios *, struct winsize *);
void velvet_window_destroy(struct velvet_window *velvet_window) {
  if (velvet_window->pty > 0) {
    close(velvet_window->pty);
    velvet_window->pty = 0;
  }
  if (velvet_window->pid > 0) {
    int status;
    kill(velvet_window->pid, SIGTERM);
    /* If a process explicitly handles SIGTERM, tell it in no uncertain terms that
     * nobody is listening. This fixes an issue when shutting down velvet while a window is being debugged
     * by lldb-dap which intercepts SIGTERM and prevents the server socket from closing */
    kill(velvet_window->pid, SIGHUP);
    pid_t result = waitpid(velvet_window->pid, &status, WNOHANG);
    if (result == -1) ERROR("waitpid:");
  }
  vte_destroy(&velvet_window->emulator);
  string_destroy(&velvet_window->cmdline);
  string_destroy(&velvet_window->title);
  string_destroy(&velvet_window->icon);
  string_destroy(&velvet_window->cwd);
  velvet_window->pty = velvet_window->pid = 0;
}

void velvet_scene_close_and_remove_window(struct velvet_scene *s, struct velvet_window *w) {
    velvet_window_destroy(w);
    velvet_scene_remove_window(s, w);
}

void velvet_window_update_title(struct velvet_window *p) {
  if (p->emulator.osc.title.len > 0) {
    string_clear(&p->title);
    string_push_string(&p->title, p->emulator.osc.title);
    return;
  }

  if (p->pty && platform.get_cwd_from_pty) {
    char buf[256] = {0};
    if (platform.get_cwd_from_pty(p->pty, buf, sizeof(buf))) {
      string_clear(&p->cwd);
      string_push_cstr(&p->cwd, buf);
      string_clear(&p->title);
      string_push_string(&p->title, p->cmdline);
      string_push_cstr(&p->title, " in ");
      string_push_string(&p->title, p->cwd);

      char *home = getenv("HOME");
      if (home) { 
        string_replace_inplace_slow(&p->title, home, "~/");
        string_replace_inplace_slow(&p->title, "~//", "~/");
      }
    }
  } else if (!p->title.len && p->cmdline.len) {
    // fallback to using the process as title
    string_clear(&p->title);
    string_push_string(&p->title, p->cmdline);
  }
}


void velvet_window_process_output(struct velvet_window *velvet_window, struct u8_slice str) {
  assert(velvet_window->emulator.ws.h == velvet_window->rect.client.h);
  assert(velvet_window->emulator.ws.w == velvet_window->rect.client.w);
  vte_process(&velvet_window->emulator, str);
}

static bool rect_same_position(struct rect b1, struct rect b2) {
  return b1.x == b2.x && b1.y == b2.y;
}

static bool rect_same_size(struct rect b1, struct rect b2) {
  return b1.w == b2.w && b1.h == b2.h;
}

void velvet_window_resize(struct velvet_window *win, struct rect outer) {
  int bw = win->border_width;
  // Refuse to go below a minimum size
  int min_size = bw * 2 + 1;
  if (outer.w < min_size) outer.w = min_size;
  if (outer.h < min_size) outer.h = min_size;

  int pixels_per_column = (int)((float)outer.x_pixel / (float)outer.w);
  int pixels_per_row = (int)((float)outer.y_pixel / (float)outer.h);

  struct rect inner = {
      .x = outer.x + bw,
      .y = outer.y + bw,
      .w = outer.w - 2 * bw,
      .h = outer.h - 2 * bw,
      .x_pixel = inner.w * pixels_per_column,
      .y_pixel = inner.h * pixels_per_row,
  };

  if (!rect_same_size(win->rect.client, inner)) {
    struct winsize ws = {.ws_col = inner.w, .ws_row = inner.h, .ws_xpixel = inner.x_pixel, .ws_ypixel = inner.y_pixel};
    if (win->pty) ioctl(win->pty, TIOCSWINSZ, &ws);
    if (win->pid) kill(win->pid, SIGWINCH);
  }

  bool send_resize = !rect_same_size(win->rect.window, outer) && win->events.on_resize;
  bool send_move = !rect_same_position(win->rect.window, outer) && win->events.on_move;

  win->rect.window = outer;
  win->rect.client = inner;

  vte_set_size(&win->emulator, inner);

  if (send_resize)
    win->events.on_resize(win->id, win->events.data);
  if (send_move)
    win->events.on_move(win->id, win->events.data);
}

bool velvet_window_start(struct velvet_window *velvet_window) {
  struct rect c = velvet_window->rect.client;
  struct winsize velvet_windowsize = {.ws_col = c.w, .ws_row = c.h, .ws_xpixel = c.x_pixel, .ws_ypixel = c.y_pixel};

  /* block signal generation in the child. This is important because signals delivered between fork() and exec() will be
   * delivered to the parent because the installed signal handlers write to a pipe which is shared across fork(). */
  sigset_t block, sighandler;
  sigfillset(&block);
  sigprocmask(SIG_BLOCK, &block, &sighandler);

  pid_t pid = forkpty(&velvet_window->pty, nullptr, nullptr, &velvet_windowsize);

  /* immediately restore the original signal handler in the parent */
  if (pid != 0) {
    sigprocmask(SIG_SETMASK, &sighandler, NULL);
  }

  if (pid < 0) {
    ERROR("Unable to spawn process:");
    return false;
  }

  if (pid == 0) {
    if (velvet_window->cwd.len) {
      string_ensure_null_terminated(&velvet_window->cwd);
      if (chdir((char*)velvet_window->cwd.content) == -1) {
        ERROR("chdir:");
      }
    }

    string_ensure_null_terminated(&velvet_window->cmdline);
    char id[20];
    snprintf(id, sizeof(id) - 1, "%d", velvet_window->id);
    setenv("VELVET_WINID", id, true);
    char *argv[] = {"sh", "-c", (char*)velvet_window->cmdline.content, NULL};
    execvp("sh", argv);
    velvet_die("execlp:");
  }
  velvet_window->pid = pid;
  set_nonblocking(velvet_window->pty);
  fcntl(velvet_window->pty, F_SETFD, FD_CLOEXEC);
  return true;
}

void velvet_scene_destroy(struct velvet_scene *m) {
  struct velvet_window *h;
  vec_foreach(h, m->windows) {
    velvet_window_destroy(h);
  }
  vec_destroy(&m->windows);
  vec_destroy(&m->focus_order);
  velvet_render_destroy(&m->renderer);
}
