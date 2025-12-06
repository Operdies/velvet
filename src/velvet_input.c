#include "velvet_input.h"
#include "csi.h"
#include "utils.h"
#include "virtual_terminal_sequences.h"

#define ESC 0x1b
#define BRACKETED_PASTE_MAX (1 << 20)
#define CSI_BUFFER_MAX (256)

static const struct u8_slice bracketed_paste_start = STRING_SLICE(u8"\x1b[200~");
static const struct u8_slice bracketed_paste_end = STRING_SLICE(u8"\x1b[201~");

enum scroll_direction {
  scroll_up = 0,
  scroll_down = 1,
  scroll_left = 2,
  scroll_right = 3,
};

enum mouse_state {
  mouse_left = 0,
  mouse_middle = 1,
  mouse_right = 2,
  mouse_none = 3,
};
enum mouse_modifiers {
  modifier_none = 0,
  modifier_shift = 0x04,
  modifier_alt = 0x08,
  modifier_ctrl = 0x10,
};
enum mouse_event {
  mouse_click = 0,
  mouse_move = 0x20,
  mouse_scroll = 0x40,
};
enum mouse_trigger {
  mouse_down,
  mouse_up,
};

struct mouse_sgr {
  union {
    enum scroll_direction scroll_direction;
    enum mouse_state button_state;
  };
  enum mouse_modifiers modifiers;
  enum mouse_event event_type;
  enum mouse_trigger trigger;
  int row, column;
};

#define send_bytes(in, ...)                                                                                            \
  send(in, (struct u8_slice){.len = sizeof((uint8_t[]){__VA_ARGS__}), .content = (uint8_t[]){__VA_ARGS__}})

static void send(struct velvet_input *in, struct u8_slice s) {
  assert(in->m->hosts.length > 0);
  struct vte_host *focus = vec_nth(&in->m->hosts, in->m->focus);
  string_push_slice(&focus->vte.pending_input, s);
}

static void send_byte(struct velvet_input *in, uint8_t ch) {
  send_bytes(in, ch);
}

static void dispatch_normal(struct velvet_input *in, uint8_t ch) {
  assert(in->command_buffer.len == 0);
  if (ch == in->prefix) {
    in->state = VELVET_INPUT_STATE_PREFIX;
    return;
  }
  switch (ch) {
  case ESC: {
    string_push_char(&in->command_buffer, ch);
    in->state = VELVET_INPUT_STATE_ESC;
  } break;
  default: send_byte(in, ch); break;
  }
}

static void send_csi_todo(struct velvet_input *in) {
  TODO("Input CSI: %.*s", in->command_buffer.len, in->command_buffer.content);
  string_clear(&in->command_buffer);
}

static void DISPATCH_FOCUS_OUT(struct velvet_input *in, const struct csi *const c);
static void DISPATCH_FOCUS_IN(struct velvet_input *in, const struct csi *const c);
static void DISPATCH_SGR_MOUSE(struct velvet_input *in, const struct csi *const c);
static void DISPATCH_ARROW_KEY_UP(struct velvet_input *in, const struct csi *const c);
static void DISPATCH_ARROW_KEY_DOWN(struct velvet_input *in, const struct csi *const c);
static void DISPATCH_ARROW_KEY_LEFT(struct velvet_input *in, const struct csi *const c);
static void DISPATCH_ARROW_KEY_RIGHT(struct velvet_input *in, const struct csi *const c);

static void mouse_debug_logging(struct mouse_sgr sgr) {
  char *mousebuttons[] = {"left", "middle", "right", "none"};
  char *mousebutton = mousebuttons[sgr.button_state];
  char *scrolldirs[] = {"up", "down", "left", "right"};
  char *scrolldir = scrolldirs[sgr.scroll_direction];
  char *eventname = sgr.event_type & mouse_scroll ? "scroll"
                    : sgr.event_type & mouse_move ? "move"
                    : sgr.trigger == mouse_down   ? "click"
                                                  : "release";

  logmsg("shift=%d,alt=%d,ctrl=%d",
         !!(sgr.modifiers & modifier_shift),
         !!(sgr.modifiers & modifier_alt),
         !!(sgr.modifiers & modifier_ctrl));
  switch (sgr.event_type) {
  case mouse_click: logmsg("%s %s at %d;%d", eventname, mousebutton, sgr.row, sgr.column); break;
  case mouse_move: logmsg("%s at %d;%d", eventname, sgr.row, sgr.column); break;
  case mouse_scroll: logmsg("%s %s %d;%d", eventname, scrolldir, sgr.row, sgr.column); break;
  }
}

struct vte_host *coord_to_client(struct velvet_input *in, struct mouse_sgr sgr) {
  for (size_t i = 0; i < in->m->hosts.length; i++) {
    struct vte_host *h = vec_nth(&in->m->hosts, i);
    struct bounds b = h->rect.window;
    if (b.x <= sgr.column && (b.x + b.w) >= sgr.column && b.y <= sgr.row && (b.y + b.h) >= sgr.row) {
      return h;
      break;
    }
  }
  return nullptr;
}

struct mouse_sgr mouse_sgr_from_csi(const struct csi *const c) {
  int btn = c->params[0].primary;
  int col = c->params[1].primary;
  int row = c->params[2].primary;

  enum mouse_state mouse_button = btn & 0x03;
  enum mouse_modifiers modifiers = btn & (0x04 | 0x08 | 0x10);
  enum mouse_event event_type = btn & (0x20 | 0x40);

  struct mouse_sgr sgr = {
      .button_state = mouse_button,
      .modifiers = modifiers,
      .event_type = event_type,
      .row = row,
      .column = col,
      .trigger = c->final == 'M' ? mouse_down : mouse_up,
  };
  return sgr;
}

static void send_mouse_sgr(struct vte_host *target, struct mouse_sgr sgr) {
  struct mouse_sgr trans = sgr;
  trans.row = sgr.row - target->rect.client.y;
  trans.column = sgr.column - target->rect.client.x;

  int btn = trans.button_state | trans.modifiers | trans.event_type;
  int start = target->vte.pending_input.len;
  string_push_csi(&target->vte.pending_input,
                  '<',
                  INT_SLICE(btn, trans.column, trans.row),
                  trans.trigger == mouse_down ? "M" : "m");
  int end = target->vte.pending_input.len;
  struct u8_slice s = string_range(&target->vte.pending_input, start, end);
  logmsg("send sgr: %.*s", s.len, s.content);
}

static void send_csi_mouse(struct velvet_input *in, const struct csi *const c) {
  struct mouse_sgr sgr = mouse_sgr_from_csi(c);

  mouse_debug_logging(sgr);

  if (in->options.focus_follows_mouse) {
    if (sgr.event_type & mouse_move && sgr.button_state == mouse_none) {
      for (size_t i = 0; i < in->m->hosts.length; i++) {
        struct vte_host *h = vec_nth(&in->m->hosts, i);
        struct bounds b = h->rect.window;
        if (b.x <= sgr.column && (b.x + b.w) >= sgr.column && b.y <= sgr.row && (b.y + b.h) >= sgr.row) {
          multiplexer_set_focus(in->m, i);
          break;
        }
      }
    }
  }

  struct vte_host *target = coord_to_client(in, sgr);
  if (!target) return;

  struct mouse_options m = target->vte.options.mouse;
  logmsg("Target: %s", target->title);
  logmsg("Target tracking: %d", m.tracking);
  logmsg("Target mode: %d", m.mode);
  if (m.tracking == MOUSE_TRACKING_OFF || m.tracking == MOUSE_TRACKING_LEGACY) return;
  if (m.mode != MOUSE_MODE_SGR) return;

  bool do_send =
      (m.tracking == MOUSE_TRACKING_ALL_MOTION) ||
      ((m.tracking == MOUSE_TRACKING_CLICK || m.tracking == MOUSE_TRACKING_CELL_MOTION) &&
       sgr.event_type == mouse_click) ||
      (m.tracking == MOUSE_TRACKING_CELL_MOTION && sgr.event_type == mouse_move && sgr.button_state != mouse_none);
  // TODO: scroll
  if (do_send) {
    send_mouse_sgr(target, sgr);
  }
}

static void dispatch_csi(struct velvet_input *in, uint8_t ch) {
  string_push_char(&in->command_buffer, ch);
  if (in->command_buffer.len > CSI_BUFFER_MAX) {
    ERROR("CSI max exceeded!!");
    string_clear(&in->command_buffer);
    in->state = VELVET_INPUT_STATE_NORMAL;
    return;
  }

  if (in->command_buffer.len == bracketed_paste_start.len &&
      string_starts_with(&in->command_buffer, bracketed_paste_start)) {
    in->state = VELVET_INPUT_STATE_BRACKETED_PASTE;
    return;
  }

  // TODO: Is this accurate?
  if (ch >= 0x40 && ch <= 0x7E) {
    if (!in->m->hosts.length) return;
    struct csi c = {0};
    struct u8_slice s = string_range(&in->command_buffer, 2, -1);
    size_t len = csi_parse(&c, s);
    if (c.state == CSI_ACCEPT) {
      logmsg("Dispatch csi %.*s", s.len, s.content);
      assert(len == s.len);


#define KEY(leading, intermediate, final)                                                                              \
  ((((uint32_t)leading) << 16) | (((uint32_t)intermediate) << 8) | (((uint32_t) final)))

      switch (KEY(c.leading, c.intermediate, c.final)) {
#define CSI(l, i, f, fn, _)                                                                                            \
  case KEY(l, i, f): DISPATCH_##fn(in, &c); break;
#include "input_csi.def"
      default: send_csi_todo(in); break;
      }
    }
    string_clear(&in->command_buffer);
    in->state = VELVET_INPUT_STATE_NORMAL;
  }
}

static void dispatch_esc(struct velvet_input *in, uint8_t ch) {
  switch (ch) {
  case '[': {
    string_push_char(&in->command_buffer, ch);
    in->state = VELVET_INPUT_STATE_CSI;
  } break;
  default: {
    send_bytes(in, ESC, ch);
    in->state = VELVET_INPUT_STATE_NORMAL;
    string_clear(&in->command_buffer);
  } break;
  }
}

static void dispatch_prefix(struct velvet_input *in, uint8_t ch) {
  in->state = VELVET_INPUT_STATE_NORMAL;
  switch (ch) {
  default: break;
  }
}

static void send_bracketed_paste(struct velvet_input *in) {
  struct vte_host *focus = vec_nth(&in->m->hosts, in->m->focus);
  bool enclose = focus->vte.options.bracketed_paste;
  uint8_t *start = in->command_buffer.content;
  size_t len = in->command_buffer.len;
  if (!enclose) {
    start += bracketed_paste_start.len;
    len -= bracketed_paste_start.len + bracketed_paste_end.len;
  }
  struct u8_slice s = {.content = start, .len = len};
  string_push_slice(&focus->vte.pending_input, s);
  string_clear(&in->command_buffer);
}

static void dispatch_bracketed_paste(struct velvet_input *in, uint8_t ch) {
  string_push_char(&in->command_buffer, ch);

  if (in->command_buffer.len > BRACKETED_PASTE_MAX) {
    ERROR("Bracketed Paste max exceeded!!");
    string_clear(&in->command_buffer);
    in->state = VELVET_INPUT_STATE_NORMAL;
    return;
  }

  if (string_ends_with(&in->command_buffer, bracketed_paste_end)) {
    send_bracketed_paste(in);
    in->state = VELVET_INPUT_STATE_NORMAL;
  }
}

void velvet_input_process(struct velvet_input *in, struct u8_slice str) {
  for (size_t i = 0; i < str.len; i++) {
    uint8_t ch = str.content[i];
    switch (in->state) {
    case VELVET_INPUT_STATE_NORMAL: dispatch_normal(in, ch); break;
    case VELVET_INPUT_STATE_ESC: dispatch_esc(in, ch); break;
    case VELVET_INPUT_STATE_CSI: dispatch_csi(in, ch); break;
    case VELVET_INPUT_STATE_PREFIX:
    case VELVET_INPUT_STATE_PREFIX_CONT: dispatch_prefix(in, ch); break;
    case VELVET_INPUT_STATE_BRACKETED_PASTE: dispatch_bracketed_paste(in, ch); break;
    }
  }
  if (in->state == VELVET_INPUT_STATE_ESC) {
    in->state = VELVET_INPUT_STATE_NORMAL;
    string_clear(&in->command_buffer);
    send_byte(in, ESC);
  }
}

void velvet_input_destroy(struct velvet_input *in) {
  string_destroy(&in->command_buffer);
}

static void dispatch_focus(struct velvet_input *in, const struct csi *const c) {
  struct vte_host *focus = vec_nth(&in->m->hosts, in->m->focus);
  if (focus->vte.options.focus_reporting) send(in, c->final == 'O' ? vt_focus_out : vt_focus_in);
}

void DISPATCH_FOCUS_OUT(struct velvet_input *in, const struct csi *const c) {
  dispatch_focus(in, c);
}
void DISPATCH_FOCUS_IN(struct velvet_input *in, const struct csi *const c) {
  dispatch_focus(in, c);
}
void DISPATCH_SGR_MOUSE(struct velvet_input *in, const struct csi *const c) {
  send_csi_mouse(in, c);
}
static void dispatch_arrow_key(struct velvet_input *in, const struct csi *const c) {
  struct vte_host *focus = vec_nth(&in->m->hosts, in->m->focus);
  if (focus->vte.options.application_mode) {
    send_bytes(in, ESC, 'O', c->final);
  } else {
    send(in, string_as_u8_slice(&in->command_buffer));
  }
}
void DISPATCH_ARROW_KEY_UP(struct velvet_input *in, const struct csi *const c) {
  dispatch_arrow_key(in, c);
}
void DISPATCH_ARROW_KEY_DOWN(struct velvet_input *in, const struct csi *const c) {
  dispatch_arrow_key(in, c);
}
void DISPATCH_ARROW_KEY_LEFT(struct velvet_input *in, const struct csi *const c) {
  dispatch_arrow_key(in, c);
}
void DISPATCH_ARROW_KEY_RIGHT(struct velvet_input *in, const struct csi *const c) {
  dispatch_arrow_key(in, c);
}
