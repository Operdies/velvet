#include "csi.h"
#include "platform.h"
#include "utf8proc/utf8proc.h"
#include "utils.h"
#include "velvet.h"
#include "velvet_keyboard.h"
#include "virtual_terminal_sequences.h"
#include <string.h>

#define ESC 0x1b
#define BRACKETED_PASTE_MAX (1 << 20)
#define CSI_BUFFER_MAX (256)

#ifndef CTRL
#define CTRL(x) ((x) & 037)
#endif

static const uint8_t bracketed_paste_start[] = {0x1b, '[', '2', '0', '0', '~'};
static const uint8_t bracketed_paste_end[] = {0x1b, '[', '2', '0', '1', '~'};

enum mouse_modifiers { modifier_none = 0, modifier_shift = 4, modifier_alt = 8, modifier_ctrl = 16 };
enum mouse_event { mouse_click = 0, mouse_move = 0x20, mouse_scroll = 0x40 };

struct mouse_sgr {
  union {
    enum velvet_api_scroll_direction scroll_direction;
    enum velvet_api_mouse_button button_state;
  };
  enum mouse_modifiers modifiers;
  enum mouse_event event_type;
  enum velvet_api_mouse_event_type click_state;
  int row, column;
};

#define send_bytes(sink, ...)                                                                                          \
  send(sink, (struct u8_slice){.len = sizeof((uint8_t[]){__VA_ARGS__}), .content = (uint8_t[]){__VA_ARGS__}})

static void send(struct velvet_window *sink, struct u8_slice s) {
  if (sink) string_push_slice(&sink->emulator.pending_input, s);
}

static void send_byte(struct velvet_window *sink, uint8_t ch) {
  send_bytes(sink, ch);
}

static bool find_key(char *name, struct velvet_key *key) {
  assert(key);
  for (int i = 0; i < LENGTH(named_keys); i++) {
    struct velvet_key n = named_keys[i];
    if (n.name && strcasecmp(name, n.name) == 0) {
      *key = n;
      return true;
    }
  }
  return false;
}

static bool find_key_by_keycode(uint32_t keycode, struct velvet_key *key) {
  for (int i = 0; i < LENGTH(named_keys); i++) {
    struct velvet_key n = named_keys[i];
    if (n.kitty_terminator == 'u' && n.codepoint == keycode) {
      *key = n;
      return true;
    }
  }
  return false;
}

static void dispatch_key_event(struct velvet *v, struct velvet_key_event key);

static struct velvet_key_event key_event_from_codepoint(uint32_t cp) {
  static char shift_table[] = {
      ['!'] = '1',  ['@'] = '2', ['#'] = '3', ['$'] = '4', ['%'] = '5', ['^'] = '6', ['&'] = '7',
      ['*'] = '8',  ['('] = '9', [')'] = '0', ['<'] = ',', ['>'] = '.', [':'] = ';', ['"'] = '\'',
      ['|'] = '\\', ['~'] = '`', ['?'] = '/', ['{'] = '[', ['}'] = ']',
  };

  // special case for <C-Space>
  if (cp == 0) {
    struct velvet_key_event e = {.modifiers = VELVET_API_KEY_MODIFIER_CONTROL};
    find_key_by_keycode(' ', &e.key);
    return e;
  }

  struct velvet_key_event k = {0};

  bool iscntrl = CTRL(cp) == cp && !((cp == '\t' || cp == '\r' || cp == '\b' || cp == ESC));
  if (iscntrl) {
    cp = cp + 96;
  }

  bool isshift = false;
  if (!iscntrl) {
    uint32_t upper = utf8proc_toupper(cp);
    uint32_t lower = utf8proc_tolower(cp);
    if (cp == upper && upper != lower) {
      isshift = true;
      k.key.alternate_codepoint = upper;
      cp = lower;
    }

    if (!isshift) {
      isshift = (cp < LENGTH(shift_table)) && shift_table[cp];
      if (isshift) {
        k.key.alternate_codepoint = cp;
        cp = shift_table[cp];
      }
    }
  }

  k.key.codepoint = cp;
  /* populate the "name" field if possible. This is needed for characters such as escape, tab, backspace etc. */
  find_key_by_keycode(cp, &k.key);
  k.modifiers = ((iscntrl * VELVET_API_KEY_MODIFIER_CONTROL) | (isshift * VELVET_API_KEY_MODIFIER_SHIFT));
  k.key.kitty_terminator = 'u';
  k.type = VELVET_API_KEY_EVENT_TYPE_PRESS;

  uint32_t associated_text = k.key.alternate_codepoint ? k.key.alternate_codepoint : k.key.codepoint;
  k.associated_text.n = 1;
  k.associated_text.codepoints[0] = associated_text;
  return k;
}

static void DISPATCH_KITTY_KEY(struct velvet *v, struct csi c);
static void DISPATCH_FOCUS_OUT(struct velvet *v, struct csi c);
static void DISPATCH_FOCUS_IN(struct velvet *v, struct csi c);
static void DISPATCH_SGR_MOUSE(struct velvet *v, struct csi c);

static struct mouse_sgr mouse_sgr_from_csi(struct csi c) {
  int btn = c.params[0].primary;
  int col = c.params[1].primary;
  int row = c.params[2].primary;

  enum velvet_api_mouse_button mouse_button = btn & 0x03;
  enum mouse_modifiers modifiers = btn & (0x04 | 0x08 | 0x10);
  enum mouse_event event_type = btn & (0x20 | 0x40);

  struct mouse_sgr sgr = {
      .button_state = mouse_button,
      .modifiers = modifiers,
      .event_type = event_type,
      .row = row,
      .column = col,
      .click_state = c.final == 'M' ? VELVET_API_MOUSE_EVENT_TYPE_MOUSE_DOWN : VELVET_API_MOUSE_EVENT_TYPE_MOUSE_UP,
  };
  return sgr;
}

static void send_mouse_sgr(struct velvet_window *target, struct mouse_sgr trans) {
  int btn = trans.button_state | trans.modifiers | trans.event_type;
  if (target->emulator.options.mouse.mode == MOUSE_MODE_SGR) {
    string_push_csi(&target->emulator.pending_input,
                    '<',
                    INT_SLICE(btn, trans.column, trans.row),
                    trans.click_state == VELVET_API_MOUSE_EVENT_TYPE_MOUSE_DOWN ? "M" : "m");
  } else if (target->emulator.options.mouse.mode == MOUSE_MODE_DEFAULT) {
    /* CSI M Cb Cx Cy */
    uint8_t Cb = btn | trans.modifiers;
    uint8_t Cx = MIN(32 + trans.column, 255);
    uint8_t Cy = MIN(32 + trans.row, 255);
    string_push_cstr(&target->emulator.pending_input, "\x1b[M");
    string_push_char(&target->emulator.pending_input, Cb);
    string_push_char(&target->emulator.pending_input, Cx);
    string_push_char(&target->emulator.pending_input, Cy);
  } else {
    TODO("Mouse mode %d not implemented.", target->emulator.options.mouse.mode);
  }
}

static void velvet_input_send_vk(struct velvet *v, struct velvet_key_event e);

static void scroll_to_bottom(struct velvet_window *focus) {
  if (focus) screen_set_scroll_offset(&focus->emulator.primary, 0);
}

static void window_paste(struct velvet_window *w, struct u8_slice text) {
  bool enclose = w->emulator.options.bracketed_paste;
  if (enclose) string_push_range(&w->emulator.pending_input, bracketed_paste_start, sizeof(bracketed_paste_start));
  string_push_slice(&w->emulator.pending_input, text);
  if (enclose) string_push_range(&w->emulator.pending_input, bracketed_paste_end, sizeof(bracketed_paste_end));
}

static void send_bracketed_paste(struct velvet *v) {
  struct velvet_input *in = &v->input;
  struct velvet_window *focus = velvet_scene_get_focus(&v->scene);
  if (focus) {
    uint8_t *start = in->command_buffer.content;
    size_t len = in->command_buffer.len;
    struct u8_slice paste = { .content=  start, .len = len };
    paste = u8_slice_range(paste, sizeof(bracketed_paste_start), paste.len - sizeof(bracketed_paste_end));
    window_paste(focus, paste);
  }
  string_clear(&v->input.command_buffer);
  in->state = VELVET_INPUT_STATE_NORMAL;
  scroll_to_bottom(focus);
}

static void dispatch_csi(struct velvet *v, uint8_t ch) {
  struct velvet_input *in = &v->input;
  string_push_char(&v->input.command_buffer, ch);

  struct string *b = &v->input.command_buffer;
  if (b->len >= sizeof(bracketed_paste_start) &&
      memcmp(b->content, bracketed_paste_start, sizeof(bracketed_paste_start)) == 0) {
    if (memcmp(b->content + b->len - sizeof(bracketed_paste_end), bracketed_paste_end, sizeof(bracketed_paste_end)) ==
        0) {
      send_bracketed_paste(v);
    }

    if (in->command_buffer.len > BRACKETED_PASTE_MAX) {
      ERROR("Bracketed Paste max exceeded!!");
      string_clear(&v->input.command_buffer);
      in->state = VELVET_INPUT_STATE_NORMAL;
      return;
    }
    return;
  }

  if (in->command_buffer.len > CSI_BUFFER_MAX) {
    ERROR("CSI max exceeded!!");
    string_clear(&v->input.command_buffer);
    in->state = VELVET_INPUT_STATE_NORMAL;
    return;
  }

  // TODO: Is this accurate?
  if (ch >= 0x40 && ch <= 0x7E) {
    struct csi c = {0};
    struct u8_slice s = string_range(&v->input.command_buffer, 2, -1);
    size_t len = csi_parse(&c, s);
    if (c.state == CSI_ACCEPT) {
      assert(len == s.len);

#define KEY(leading, intermediate, final)                                                                              \
  ((((uint32_t)leading) << 16) | (((uint32_t)intermediate) << 8) | (((uint32_t) final)))

      switch (KEY(c.leading, c.intermediate, c.final)) {
#define CSI(l, i, f, fn, _)                                                                                            \
  case KEY(l, i, f): DISPATCH_##fn(v, c); break;
#include "input_csi.def"
      default: {
        struct u8_slice raw = string_as_u8_slice(v->input.command_buffer);
        for (int i = 0; i < LENGTH(named_keys); i++) {
          struct velvet_key k = named_keys[i];
          if (k.escape && u8_slice_equals(raw, u8_slice_from_cstr(k.escape))) {
            struct velvet_key_event evt = {.key = k, .legacy = true};
            dispatch_key_event(v, evt);
            break;
          }
        }
      } break;
      }
    }
    string_clear(&v->input.command_buffer);
    in->state = VELVET_INPUT_STATE_NORMAL;
  }
#undef KEY
#undef CSI
}

/* keys such as altgr */
static bool is_iso_shift(uint32_t codepoint) {
  return codepoint == 57453     /* iso level 3 shift */
         || codepoint == 57454; /* iso level 5 shift */
}

static bool is_shift(uint32_t codepoint) {
  return codepoint == 57441     /* left shift */
         || codepoint == 57447; /* right shift */
}

static bool is_ctrl(uint32_t codepoint) {
  return codepoint == 57442     /* left control */
         || codepoint == 57448; /* right control */
}

static bool is_meta(uint32_t codepoint) {
  return codepoint == 57446     /* left meta */
         || codepoint == 57452; /* right meta */
}

static bool is_alt(uint32_t codepoint) {
  return codepoint == 57443     /* left alt */
         || codepoint == 57449; /* right alt */
}

static bool is_super(uint32_t codepoint) {
  return codepoint == 57444     /* left super */
         || codepoint == 57450; /* right super */
}

static bool is_hyper(uint32_t codepoint) {
  return codepoint == 57445     /* left hyper */
         || codepoint == 57451; /* right hyper */
}

static bool is_caps_lock(uint32_t codepoint) {
  return codepoint == 57358;
}

static bool is_scroll_lock(uint32_t codepoint) {
  return codepoint == 57359;
}
static bool is_num_lock(uint32_t codepoint) {
  return codepoint == 57360;
}

static bool is_modifier(uint32_t codepoint) {
  return is_iso_shift(codepoint) || is_shift(codepoint) || is_meta(codepoint) || is_alt(codepoint) ||
         is_ctrl(codepoint) || is_super(codepoint) || is_hyper(codepoint) || is_caps_lock(codepoint) ||
         is_scroll_lock(codepoint) || is_num_lock(codepoint);
}

static bool is_modifier(uint32_t codepoint);

static void raise_key_event(struct velvet *v, struct velvet_key_event e) {
  struct u8_slice name = u8_slice_from_cstr(e.key.name);
  struct velvet_api_session_key_event_args event_args = {
      .key = {.codepoint = e.key.codepoint,
              .alternate_codepoint = e.key.alternate_codepoint,
              .event_type = e.type,
              .modifiers = e.modifiers,
              .name = {.set = e.key.name, .value = name}},
  };
  velvet_api_raise_session_on_key(v, event_args);
}

// this is supposed to emulate VIM-like behavior
static void dispatch_key_event(struct velvet *v, struct velvet_key_event e) {
  assert(v);
  if (!e.type) e.type = VELVET_API_KEY_EVENT_TYPE_PRESS;
  raise_key_event(v, e);
}

static void DISPATCH_KITTY_KEY(struct velvet *v, struct csi c) {
  uint32_t codepoint = c.params[0].primary;
  uint32_t alternate_codepoint = c.params[0].sub[0];
  uint32_t modifiers = c.params[1].primary;
  uint32_t event = c.params[1].sub[0];

  if (!codepoint) codepoint = 1;
  if (modifiers) modifiers -= 1;
  if (!event) event = VELVET_API_KEY_EVENT_TYPE_PRESS;

  for (int i = 0; i < LENGTH(named_keys); i++) {
    struct velvet_key k = named_keys[i];
    if (k.kitty_terminator != c.final) continue;
    if (alternate_codepoint) k.alternate_codepoint = alternate_codepoint;
    if (k.codepoint == codepoint) {
      struct velvet_key_event e = {.modifiers = modifiers, .key = k, .type = event};
      dispatch_key_event(v, e);
      return;
    }
  }

  struct velvet_key k = {.codepoint = codepoint, .kitty_terminator = 'u', .alternate_codepoint = alternate_codepoint};
  struct velvet_key_event e = {.modifiers = modifiers, .key = k, .type = event};

  for (int i = 2; i < c.n_params; i++) {
    int codepoint = c.params[i].primary;
    int index = e.associated_text.n++;
    if (index >= LENGTH(e.associated_text.codepoints)) {
      e.associated_text.n = 0;
      break;
    }
    e.associated_text.codepoints[index] = codepoint;
  }
  dispatch_key_event(v, e);
}

static void dispatch_app(struct velvet *v, uint8_t ch) {
  bool found = false;
  string_push_char(&v->input.command_buffer, ch);

  struct u8_slice key = string_as_u8_slice(v->input.command_buffer);
  for (int i = 0; !found && i < LENGTH(named_keys); i++) {
    struct velvet_key n = named_keys[i];
    if (!n.escape) continue;
    struct u8_slice key_escape = u8_slice_from_cstr(n.escape);
    if (u8_slice_equals(key, key_escape)) {
      struct velvet_key_event e = {.key = n, .legacy = true};
      dispatch_key_event(v, e);
      found = true;
    }
  }
  v->input.state = VELVET_INPUT_STATE_NORMAL;
  string_clear(&v->input.command_buffer);
  if (!found) velvet_log("TODO: Unhandled escape \x1bO%c", ch);
}

static void dispatch_esc(struct velvet *v, uint8_t ch) {
  string_push_char(&v->input.command_buffer, ch);
  struct velvet_input *in = &v->input;
  if (ch == '[') {
    in->state = VELVET_INPUT_STATE_CSI;
  } else if (ch == 'O') {
    in->state = VELVET_INPUT_STATE_APPLICATION_KEYS;
  } else {
    in->state = VELVET_INPUT_STATE_NORMAL;
    string_clear(&v->input.command_buffer);
    struct velvet_key_event k = key_event_from_codepoint(ch);
    /* ALT and META are different, but in VT environments they have historically
     * been collated. Since there is no way to distinguish them,
     * treat ESC as both. */
    k.modifiers |= VELVET_API_KEY_MODIFIER_ALT;
    k.modifiers |= VELVET_API_KEY_MODIFIER_META;
    k.legacy = true;
    dispatch_key_event(v, k);
  }
}

static void
velvet_input_send_vk_basic(struct velvet_window *sink, struct velvet_key vk, enum velvet_api_key_modifier m) {
  int n = 0;
  struct utf8 buf = {0};
  char *escape = NULL;
  if (m & VELVET_API_KEY_MODIFIER_CAPS_LOCK) {
    if (m & VELVET_API_KEY_MODIFIER_SHIFT)
      vk.codepoint = utf8proc_tolower(vk.codepoint);
    else
      vk.codepoint = utf8proc_toupper(vk.codepoint);
  }
  if (vk.kitty_terminator == 'u' && vk.codepoint && vk.codepoint < 255) {
    uint32_t send = vk.alternate_codepoint && vk.alternate_codepoint < 255 ? vk.alternate_codepoint : vk.codepoint;
    n = codepoint_to_utf8(send, &buf);
    escape = (char *)buf.utf8;
  } else if (vk.escape && vk.escape[0]) {
    escape = vk.escape;
    n = strlen(escape);
  }

  if (escape && escape[0]) {
    scroll_to_bottom(sink);
    if (vk.codepoint == ESC) {
      send_byte(sink, ESC);
    } else {
      bool is_meta = m & VELVET_API_KEY_MODIFIER_ALT;
      bool is_cntrl = m & VELVET_API_KEY_MODIFIER_CONTROL;

      if (is_meta) send_byte(sink, ESC);
      bool is_byte = !escape[1];
      if (is_byte && is_cntrl) {
        char ch = escape[0] & 0x1f;
        send_byte(sink, ch);
      } else {
        for (int i = 0; i < n; i++) send_byte(sink, escape[i]);
      }
    }
  }
}

static bool is_keypad(struct velvet_key k) {
  uint32_t codepoint = k.codepoint;
  uint32_t kp_0 = 57399;     /* first kp codepoint */
  uint32_t kp_begin = 57427; /* last kp codepoint */

  /* KP_BEGIN can be encoded as a non-codepoint */
  if (k.kitty_terminator == 'E' && codepoint == 1) return true;
  return kp_0 <= codepoint && codepoint <= kp_begin;
}

static bool is_keypad_with_text(struct velvet_key k) {
  uint32_t codepoint = k.codepoint;
  uint32_t kp_0 = 57399;
  uint32_t kp_separator = 57416;

  if (k.kitty_terminator == 'E' && codepoint == 1) return false;
  return kp_0 <= codepoint && codepoint <= kp_separator;
}

/* https://sw.kovidgoyal.net/kitty/keyboard-protocol/ */
static void
velvet_input_send_kitty_encoding(struct velvet_window *f, struct velvet_key_event e, enum kitty_keyboard_options o) {
  assert(f);
  if (!e.type) e.type = VELVET_API_KEY_EVENT_TYPE_PRESS;
  bool is_mod = is_modifier(e.key.codepoint);

  bool do_send = (o & KITTY_KEYBOARD_REPORT_EVENT_TYPES) || e.type < VELVET_API_KEY_EVENT_TYPE_RELEASE;
  if (is_mod && !(o & KITTY_KEYBOARD_REPORT_ALL_KEYS_AS_ESCAPE_CODES)) do_send = false;

  if (do_send && e.type == VELVET_API_KEY_EVENT_TYPE_RELEASE) {
    /* tab, backspace and return release events must only be repoted if KITTY_KEYBOARD_REPORT_ALL_KEYS_AS_ESCAPE_CODES
     * is set */
    if (e.key.kitty_terminator == 'u' &&
        (e.key.codepoint == '\t' || e.key.codepoint == '\r' || e.key.codepoint == '\b')) {
      do_send = o & KITTY_KEYBOARD_REPORT_ALL_KEYS_AS_ESCAPE_CODES;
    }
  }

  if (!do_send) return;

  /* Disambiguate escape codes description from kitty docs:
   *Turning on this flag will cause the terminal to report the Esc, alt+key, ctrl+key, ctrl+alt+key, shift+alt+key keys
   *using CSI u sequences instead of legacy ones. */
  bool do_send_encoded =
      (o & KITTY_KEYBOARD_REPORT_ALL_KEYS_AS_ESCAPE_CODES) /* self explanatory */
      || is_mod                                            /* modifers must always be encoded */
      || (e.type == VELVET_API_KEY_EVENT_TYPE_RELEASE)     /* release events must be encoded. In my mind, repeat events
                                        should also     be encoded, but this is not how other emulators do it. */
      || (e.key.escape && e.key.escape[0] == ESC) /* encode any key which is otherwise encoded with a leading ESC */
      || (is_keypad(e.key) && !is_keypad_with_text(e.key)) /* all non-text keypad keys */
      || (e.modifiers &
          (VELVET_API_KEY_MODIFIER_CONTROL | VELVET_API_KEY_MODIFIER_ALT)); /* disambiguate ctrl / alt modifiers */

  if (!do_send_encoded) {
    /* send the base symbol. This applies to most pure-text keys */
    if (!is_mod) velvet_input_send_vk_basic(f, e.key, e.modifiers);
    return;
  }

  uint32_t modifiers = e.modifiers + 1; /* modifiers are encoded as +1 */
  bool do_encode_event = (o & KITTY_KEYBOARD_REPORT_EVENT_TYPES) && e.type > VELVET_API_KEY_EVENT_TYPE_PRESS;
  bool do_encode_modifier = do_encode_event || modifiers > 1;

  enum kitty_keyboard_options flags =
      KITTY_KEYBOARD_REPORT_ASSOCIATED_TEXT | KITTY_KEYBOARD_REPORT_ALL_KEYS_AS_ESCAPE_CODES;

  /* associated text is not included on key release events */
  bool do_encode_associated_text =
      e.type != VELVET_API_KEY_EVENT_TYPE_RELEASE && (o & flags) == flags && e.associated_text.n;

  bool encode_codepoint = do_encode_associated_text || do_encode_modifier || e.key.codepoint > 1 ||
                          (o & KITTY_KEYBOARD_REPORT_ALTERNATE_KEYS && e.key.alternate_codepoint);

  struct string *s = &f->emulator.pending_input;
  string_push_cstr(s, "\x1b[");
  if (encode_codepoint) {
    string_push_int(s, e.key.codepoint);
    if (o & KITTY_KEYBOARD_REPORT_ALTERNATE_KEYS && e.key.alternate_codepoint) {
      string_push_char(s, ':');
      string_push_int(s, e.key.alternate_codepoint);
    }
  }

  if (do_encode_modifier) {
    string_push_char(s, ';');
    string_push_int(s, modifiers);
    if (do_encode_event) {
      string_push_char(s, ':');
      string_push_int(s, e.type);
    }
  }

  if (do_encode_associated_text) {
    if (!do_encode_modifier) string_push_char(s, ';');
    for (int i = 0; i < e.associated_text.n; i++) {
      string_push_char(s, ';');
      string_push_int(s, e.associated_text.codepoints[i]);
    }
  }

  string_push_char(s, e.key.kitty_terminator);
}

static void velvet_input_send_vk_to_window(struct velvet_key_event e, struct velvet_window *f, struct velvet *v) {
  assert(f);

  if (f->is_lua_window) {
    struct u8_slice name = u8_slice_from_cstr(e.key.name);
    /* keys should be sent to lua windows as discrete typed events */
    struct velvet_api_window_on_key_event_args event_args = {
        .win_id = f->id,
        .key =
            {
                .codepoint = e.key.codepoint,
                .alternate_codepoint = e.key.alternate_codepoint,
                .event_type = e.type,
                .modifiers = e.modifiers,
                .name = { .set = e.key.name, .value = name },
            },
    };
    velvet_api_raise_window_on_key(v, event_args);
  } else {
    enum kitty_keyboard_options o = f->emulator.options.kitty[f->emulator.options.alternate_screen].options;
    if (o == KITTY_KEYBOARD_NONE || e.legacy) {
      if (e.type != VELVET_API_KEY_EVENT_TYPE_RELEASE) {
        if (!is_modifier(e.key.codepoint)) {
          velvet_input_send_vk_basic(f, e.key, e.modifiers);
        }
      }
    } else {
      velvet_input_send_kitty_encoding(f, e, o);
    }
  }
}

static void velvet_input_send_vk(struct velvet *v, struct velvet_key_event e) {
  struct velvet_window *f = velvet_scene_get_focus(&v->scene);
  if (f) velvet_input_send_vk_to_window(e, f, v);
}

static void dispatch_normal(struct velvet *v, uint8_t ch) {
  struct velvet_input *in = &v->input;
  assert(in->command_buffer.len == 0);

  if (ch == ESC) {
    string_push_char(&v->input.command_buffer, ch);
    in->state = VELVET_INPUT_STATE_ESC;
    return;
  }

  struct velvet_key_event key = key_event_from_codepoint(ch);
  key.legacy = true;
  dispatch_key_event(v, key);
}

void velvet_input_process(struct velvet *v, struct u8_slice str) {
  // velvet_log("Input: %.*s (%d)", (int)str.len, str.content, (int)str.len);
  struct velvet_input *in = &v->input;
  for (size_t i = 0; i < str.len; i++) {
    uint8_t ch = str.content[i];
    switch (in->state) {
    case VELVET_INPUT_STATE_NORMAL: dispatch_normal(v, ch); break;
    case VELVET_INPUT_STATE_ESC: dispatch_esc(v, ch); break;
    case VELVET_INPUT_STATE_CSI: dispatch_csi(v, ch); break;
    case VELVET_INPUT_STATE_APPLICATION_KEYS: dispatch_app(v, ch); break;
    }
  }

  if (in->state == VELVET_INPUT_STATE_ESC) {
    in->state = VELVET_INPUT_STATE_NORMAL;
    string_clear(&v->input.command_buffer);
    struct velvet_key_event k = {0};
    find_key_by_keycode(27, &k.key);
    dispatch_key_event(v, k);
  }
}

static void dispatch_focus(struct velvet *v, struct csi c) {
  struct velvet_window *focus = velvet_scene_get_focus(&v->scene);
  if (focus && focus->emulator.options.focus_reporting) {
    send(focus, c.final == 'O' ? vt_focus_out : vt_focus_in);
  }
  if (c.final == 'I' && v->input.input_socket) v->focused_socket = v->input.input_socket;
}

void DISPATCH_FOCUS_OUT(struct velvet *v, struct csi c) {
  dispatch_focus(v, c);
}
void DISPATCH_FOCUS_IN(struct velvet *v, struct csi c) {
  dispatch_focus(v, c);
}

static void velvet_input_send_mouse_event(struct velvet *v, struct velvet_window *w, struct mouse_sgr sgr) {
  struct velvet_input *in = &v->input;
  struct mouse_options m = w->emulator.options.mouse;

  bool do_send = (m.tracking == MOUSE_TRACKING_ALL_MOTION) ||
                 ((m.tracking == MOUSE_TRACKING_CLICK || m.tracking == MOUSE_TRACKING_CELL_MOTION) &&
                  sgr.event_type == mouse_click) ||
                 (m.tracking == MOUSE_TRACKING_CELL_MOTION && sgr.event_type == mouse_move &&
                  sgr.button_state != VELVET_API_MOUSE_BUTTON_NONE) ||
                 (m.tracking && sgr.event_type == mouse_scroll);

  if (!(sgr.row > 0 && sgr.column > 0 && sgr.column <= w->geometry.width && sgr.row <= w->geometry.height)) {
    /* verify the event is actually within the client area */
    do_send = false;
    velvet_log("mouse event out of bounds");
  }

  if (do_send) {
    send_mouse_sgr(w, sgr);
    return;
  }

  if (sgr.event_type != mouse_scroll) return;

  if (w->emulator.options.alternate_screen) {
    struct velvet_key k = {0};
    /* In the absence of mouse tracking, scroll up/down is treated as arrow keys in the alternate screen */
    if (sgr.scroll_direction == VELVET_API_SCROLL_DIRECTION_UP) {
      /* NOTE: When kitty keyhandling is enabled, kitty encodes scrolling
       * as successive key press and key release events using the kitty protocol encoding,
       * whereas ghostty and alacritty uses the same encoding regardless of kitty protocol settings.
       * I don't think it matters much so we just do what's easiest.
       */
      find_key("SS3_UP", &k);
      struct velvet_key_event e = {.key = k, .type = VELVET_API_KEY_EVENT_TYPE_PRESS};
      velvet_input_send_vk(v, e);
    } else if (sgr.scroll_direction == VELVET_API_SCROLL_DIRECTION_DOWN) {
      find_key("SS3_DOWN", &k);
      struct velvet_key_event e = {.key = k, .type = VELVET_API_KEY_EVENT_TYPE_PRESS};
      velvet_input_send_vk(v, e);
    }
  } else {
    struct screen *screen = vte_get_current_screen(&w->emulator);
    /* in the primary screen, scrolling affects the current view */
    int current_offset = screen_get_scroll_offset(screen);
    if (sgr.scroll_direction == VELVET_API_SCROLL_DIRECTION_UP) {
      int num_lines = screen_get_scroll_height(screen);
      screen_set_scroll_offset(screen, MIN(num_lines, current_offset + in->options.scroll_multiplier));
    } else if (sgr.scroll_direction == VELVET_API_SCROLL_DIRECTION_DOWN) {
      screen_set_scroll_offset(screen, MAX(0, current_offset - in->options.scroll_multiplier));
    }
    if (current_offset != screen_get_scroll_offset(screen)) velvet_invalidate_render(v, "window scroll");
  }
}

static enum velvet_api_key_modifier key_mods_from_sgr_mods(enum mouse_modifiers smods) {
  enum velvet_api_key_modifier mods = 0;
  if (smods & modifier_shift) mods |= VELVET_API_KEY_MODIFIER_SHIFT;
  if (smods & modifier_alt) mods |= VELVET_API_KEY_MODIFIER_ALT;
  if (smods & modifier_ctrl) mods |= VELVET_API_KEY_MODIFIER_CONTROL;
  return mods;
}

static enum mouse_modifiers sgr_mods_from_key_mods(enum velvet_api_key_modifier mods) {
  enum mouse_modifiers smods = 0;
  if (mods & VELVET_API_KEY_MODIFIER_SHIFT) smods |= modifier_shift;
  if (mods & VELVET_API_KEY_MODIFIER_ALT) smods |= modifier_alt;
  if (mods & VELVET_API_KEY_MODIFIER_CONTROL) smods |= modifier_ctrl;
  return smods;
}

void velvet_input_send_mouse_move(struct velvet *v, struct velvet_api_mouse_move_event_args move) {
  struct mouse_sgr sgr = {.event_type = mouse_move,
                          .button_state = move.mouse_button,
                          .column = move.pos.col,
                          .click_state = VELVET_API_MOUSE_EVENT_TYPE_MOUSE_DOWN,
                          .row = move.pos.row,
                          .modifiers = sgr_mods_from_key_mods(move.modifiers)};
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, move.win_id);
  velvet_input_send_mouse_event(v, w, sgr);
}

void velvet_input_send_mouse_click(struct velvet *v, struct velvet_api_mouse_click_event_args click) {
  struct mouse_sgr sgr = {.event_type = mouse_click,
                          .button_state = click.mouse_button,
                          .column = click.pos.col,
                          .row = click.pos.row,
                          .modifiers = sgr_mods_from_key_mods(click.modifiers),
                          .click_state = click.event_type};
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, click.win_id);
  velvet_input_send_mouse_event(v, w, sgr);
}

void velvet_input_send_mouse_scroll(struct velvet *v, struct velvet_api_mouse_scroll_event_args scroll) {
  struct mouse_sgr sgr = {.event_type = mouse_scroll,
                          .scroll_direction = scroll.direction,
                          .column = scroll.pos.col,
                          .row = scroll.pos.row,
                          .click_state = VELVET_API_MOUSE_EVENT_TYPE_MOUSE_DOWN,
                          .modifiers = sgr_mods_from_key_mods(scroll.modifiers)};
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, scroll.win_id);
  velvet_input_send_mouse_event(v, w, sgr);
}

static void emit_mouse_event(struct velvet *v, struct mouse_sgr sgr, int win_id) {
  struct velvet_api_coordinate pos = {.col = sgr.column, .row = sgr.row};
  enum velvet_api_mouse_event_type evt = sgr.click_state;
  enum velvet_api_mouse_button btn = sgr.button_state;
  enum velvet_api_scroll_direction sd = sgr.scroll_direction;

  enum velvet_api_key_modifier mods = key_mods_from_sgr_mods(sgr.modifiers);

  switch (sgr.event_type) {
  case mouse_click: {
    struct velvet_api_mouse_click_event_args event_args = {
        .event_type = evt, .modifiers = mods, .mouse_button = btn, .pos = pos, .win_id = win_id};
    velvet_api_raise_mouse_click(v, event_args);
  } break;
  case mouse_move: {
    struct velvet_api_mouse_move_event_args event_args = {
        .mouse_button = btn, .modifiers = mods, .pos = pos, .win_id = win_id};
    velvet_api_raise_mouse_move(v, event_args);
  } break;
  case mouse_scroll: {
    struct velvet_api_mouse_scroll_event_args event_args = {
        .direction = sd, .modifiers = mods, .pos = pos, .win_id = win_id};
    velvet_api_raise_mouse_scroll(v, event_args);
  } break;
  }
}

void DISPATCH_SGR_MOUSE(struct velvet *v, struct csi c) {
  struct mouse_sgr sgr = mouse_sgr_from_csi(c);
  struct velvet_window_hit hit = {0};
  velvet_scene_hit(&v->scene, sgr.column - 1, sgr.row - 1, &hit, NULL, NULL);
  struct velvet_window *target = hit.win;

  emit_mouse_event(v, sgr, target ? target->id : 0);
}

void velvet_input_destroy(struct velvet_input *in) {
  string_destroy(&in->command_buffer);
}

static bool key_from_slice(struct u8_slice s, struct velvet_key *result) {
  assert(s.len > 0);

  for (int i = 0; i < LENGTH(named_keys); i++) {
    struct velvet_key n = named_keys[i];
    if (!n.name) continue;
    struct u8_slice special = u8_slice_from_cstr(n.name);
    if (u8_slice_equals_ignore_case(s, special)) {
      *result = n;
      return true;
    }
  }

  struct u8_slice_codepoint_iterator it = {.src = s};
  if (u8_slice_codepoint_iterator_next(&it)) {
    *result = key_event_from_codepoint(it.current.value).key;
    return true;
  }

  return false;
}

struct velvet_key_iterator {
  struct u8_slice src;
  size_t cursor;
  struct velvet_key_event current;
  struct u8_slice current_range;
  bool invalid;
};

static bool velvet_key_iterator_next(struct velvet_key_iterator *it) {
  struct u8_slice t = it->src;
  size_t cursor = it->cursor;
  size_t key_end = 0;

  // for (; cursor < t.len && is_whitespace(t.content[cursor]); cursor++);
  if (cursor >= t.len || it->invalid) return false;

  uint8_t ch = t.content[cursor];

  if (ch == '<') {
    size_t match = cursor + 1;
    for (; match < t.len; match++) {
      if (t.content[match] == '>') {
        key_end = match + 1;
        break;
      }
    }
  }

  if (key_end == 0 || (key_end - cursor) < 3) {
    /* basic ascii key */
    struct u8_slice_codepoint_iterator cp = {.src = u8_slice_range(t, cursor, -1)};
    if (!u8_slice_codepoint_iterator_next(&cp) || cp.cursor == 0) {
      it->invalid = true;
      it->cursor = t.len;
      it->current_range = u8_slice_range(t, cursor, cursor + 1);
      return false;
    }
    it->current = key_event_from_codepoint(cp.current.value);
    it->cursor = cursor + cp.cursor;
    it->current_range = u8_slice_range(t, cursor, cursor + cp.cursor);
    return true;
  }

  /* this is a key chord enclosed in '<>' wiht optional modifiers */
  uint32_t mods = 0;
  it->current_range = u8_slice_range(t, cursor, key_end);
  /* strip enclosing '<>' */
  struct u8_slice inner = u8_slice_range(it->current_range, 1, -2);
  struct u8_slice modifier_slices[] = {
      u8_slice_from_cstr("S-"),    // shift     -- 1 << 0
      u8_slice_from_cstr("M-"),    // alt       -- 1 << 1
      u8_slice_from_cstr("C-"),    // ctrl      -- 1 << 2
      u8_slice_from_cstr("D-"),    // super     -- 1 << 3
      u8_slice_from_cstr("H-"),    // hyper     -- 1 << 4
      u8_slice_from_cstr("T-"),    // meta      -- 1 << 5
      u8_slice_from_cstr("Caps-"), // caps lock -- 1 << 6
      u8_slice_from_cstr("Num-"),  // num lock  -- 1 << 7
  };

  bool changed = true;
  while (changed && inner.len > 0) {
    changed = false;
    for (int i = 0; i < LENGTH(modifier_slices); i++) {
      struct u8_slice mod = modifier_slices[i];
      if (u8_slice_starts_with(inner, mod)) {
        inner = u8_slice_range(inner, mod.len, -1);
        mods |= (1 << i);
        changed = true;
      }
    }
  }

  struct velvet_key key = {0};
  if (!key_from_slice(inner, &key)) {
    it->invalid = true;
    it->cursor = t.len;
    it->current_range = u8_slice_range(t, cursor, key_end - cursor);
    return false;
  }
  it->current = (struct velvet_key_event){.key = key, .modifiers = mods};
  it->cursor = key_end;
  return true;
}

void velvet_input_send_keys(struct velvet *in, struct u8_slice str, int win_id) {
  struct velvet_window *win = velvet_scene_get_window_from_id(&in->scene, win_id);
  if (!win) return;
  struct velvet_key_iterator it = {.src = str};
  struct velvet_key_iterator copy = it;
  for (; velvet_key_iterator_next(&copy);) {
    if (it.invalid) {
      velvet_log("Put: rejecting invalid sequence %.*s", (int)str.len, str.content);
    }
  }
  for (; velvet_key_iterator_next(&it);) {
    struct velvet_key_event evt = it.current;
    evt.type = VELVET_API_KEY_EVENT_TYPE_PRESS;
    velvet_input_send_vk_to_window(evt, win, in);
    evt.type = VELVET_API_KEY_EVENT_TYPE_RELEASE;
    velvet_input_send_vk_to_window(evt, win, in);
  }
}

void velvet_input_paste_text(struct velvet *in, struct u8_slice str, int win_id) {
  struct velvet_window *win = velvet_scene_get_window_from_id(&in->scene, win_id);
  if (win) window_paste(win, str);
  
}

static struct velvet_key_event key_event_from_api_key(struct velvet_api_window_key_event e) {
  struct velvet_key_event k = {
    .key = { .codepoint = e.codepoint, .alternate_codepoint = e.alternate_codepoint, .kitty_terminator = 'u' },
    .type = e.event_type,
    .modifiers = e.modifiers,
  };

  if (e.name.set && e.name.value.content) {
    struct velvet_key out;
    if (key_from_slice(e.name.value, &out)) {
      k.key = out;
    }
  } else {
    uint32_t associated_text = e.alternate_codepoint ? e.alternate_codepoint : e.codepoint;
    k.associated_text.n = 1;
    k.associated_text.codepoints[0] = associated_text;
  }
  return k;
}

void velvet_input_send_key_event(struct velvet *v, struct velvet_api_window_key_event key_event, int win_id) {
  struct velvet_window *win = velvet_scene_get_window_from_id(&v->scene, win_id);
  velvet_input_send_vk_to_window(key_event_from_api_key(key_event), win, v);
}
