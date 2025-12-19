#include "csi.h"
#include "utils.h"
#include "virtual_terminal_sequences.h"
#include "velvet.h"
#include <string.h>

#define ESC 0x1b
#define BRACKETED_PASTE_MAX (1 << 20)
#define CSI_BUFFER_MAX (256)

#ifndef CTRL
#define CTRL(x) ((x) & 037)
#endif

static const struct u8_slice bracketed_paste_start = STRING_SLICE(u8"\x1b[200~");
static const struct u8_slice bracketed_paste_end = STRING_SLICE(u8"\x1b[201~");

enum scroll_direction { scroll_up = 0, scroll_down = 1, scroll_left = 2, scroll_right = 3 };
enum mouse_modifiers { modifier_none = 0, modifier_shift = 4, modifier_alt = 8, modifier_ctrl = 16 };
enum mouse_state { mouse_left = 0, mouse_middle = 1, mouse_right = 2, mouse_none = 3 };
enum mouse_event { mouse_click = 0, mouse_move = 0x20, mouse_scroll = 0x40 };
enum mouse_trigger { mouse_down, mouse_up };

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

static void send(struct velvet *v, struct u8_slice s) {
  assert(v->scene.hosts.length > 0);
  struct pty_host *focus = vec_nth(&v->scene.hosts, v->scene.focus);
  string_push_slice(&focus->emulator.pending_input, s);
}

static void send_byte(struct velvet *v, uint8_t ch) {
  send_bytes(v, ch);
}

static void dispatch_key_event(struct velvet *v, struct velvet_key_event key);
static struct velvet_key_event key_event_from_byte(uint8_t ch) {
  // special case for <C-Space>
  if (ch == 0)
    return (struct velvet_key_event){.key.literal = true, .key.symbol.numeric = ' ', .modifiers = MODIFIER_CTRL};

  struct velvet_key_event k = {0};
  bool iscntrl = CTRL(ch) == ch;
  if (iscntrl) {
    ch = ch + 96;
  }

  bool isshift = ch >= 'A' && ch <= 'Z';
  if (!isshift) {
  // TODO: locale aware shift table
    bool shift_table[] = {
        ['!'] = '1', ['@'] = 2, ['#'] = 3, ['$'] = 4, ['%'] = 5, ['^'] = 6, ['&'] = 7, ['*'] = 8, ['('] = 9, [')'] = 0};
    isshift = (ch < LENGTH(shift_table)) && shift_table[ch];
  }

  k.key.symbol.utf8[0] = ch;
  k.key.literal = true;
  k.modifiers = ((iscntrl * MODIFIER_CTRL) | (isshift * MODIFIER_SHIFT));
  return k;
}

static void send_csi_todo(struct velvet *v) {
  struct velvet_input *in = &v->input;
  TODO("Input CSI: %.*s", (int)in->command_buffer.len, in->command_buffer.content);
  string_clear(&v->input.command_buffer);
}

static void DISPATCH_FOCUS_OUT(struct velvet *v, const struct csi *const c);
static void DISPATCH_FOCUS_IN(struct velvet *v, const struct csi *const c);
static void DISPATCH_SGR_MOUSE(struct velvet *v, const struct csi *const c);
static void DISPATCH_ARROW_KEY_UP(struct velvet *v, const struct csi *const c);
static void DISPATCH_ARROW_KEY_DOWN(struct velvet *v, const struct csi *const c);
static void DISPATCH_ARROW_KEY_LEFT(struct velvet *v, const struct csi *const c);
static void DISPATCH_ARROW_KEY_RIGHT(struct velvet *v, const struct csi *const c);

static void mouse_debug_logging(struct mouse_sgr sgr) {
  char *mousebuttons[] = {"left", "middle", "right", "none"};
  char *mousebutton = mousebuttons[sgr.button_state];
  char *scrolldirs[] = {"up", "down", "left", "right"};
  char *scrolldir = scrolldirs[sgr.scroll_direction];
  char *eventname = sgr.event_type & mouse_scroll ? "scroll"
                    : sgr.event_type & mouse_move ? "move"
                    : sgr.trigger == mouse_down   ? "click"
                                                  : "release";

  velvet_log("shift=%d,alt=%d,ctrl=%d",
         !!(sgr.modifiers & modifier_shift),
         !!(sgr.modifiers & modifier_alt),
         !!(sgr.modifiers & modifier_ctrl));
  switch (sgr.event_type) {
  case mouse_click: velvet_log("%s %s at %d;%d", eventname, mousebutton, sgr.row, sgr.column); break;
  case mouse_move: velvet_log("%s at %d;%d", eventname, sgr.row, sgr.column); break;
  case mouse_scroll: velvet_log("%s %s %d;%d", eventname, scrolldir, sgr.row, sgr.column); break;
  }
}

struct pty_host *coord_to_client(struct velvet *v, struct mouse_sgr sgr) {
  for (size_t i = 0; i < v->scene.hosts.length; i++) {
    struct pty_host *h = vec_nth(&v->scene.hosts, i);
    struct bounds b = h->rect.window;
    if (b.x <= sgr.column && (b.x + b.columns) >= sgr.column && b.y <= sgr.row && (b.y + b.lines) >= sgr.row) {
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

static void send_mouse_sgr(struct pty_host *target, struct mouse_sgr sgr) {
  struct mouse_sgr trans = sgr;
  trans.row = sgr.row - target->rect.client.y;
  trans.column = sgr.column - target->rect.client.x;

  int btn = trans.button_state | trans.modifiers | trans.event_type;
  int start = target->emulator.pending_input.len;
  string_push_csi(&target->emulator.pending_input,
                  '<',
                  INT_SLICE(btn, trans.column, trans.row),
                  trans.trigger == mouse_down ? "M" : "m");
  int end = target->emulator.pending_input.len;
  struct u8_slice s = string_range(&target->emulator.pending_input, start, end);
  velvet_log("send sgr: %.*s", (int)s.len, s.content);
}

static void send_csi_mouse(struct velvet *v, const struct csi *const c) {
  struct mouse_sgr sgr = mouse_sgr_from_csi(c);

  mouse_debug_logging(sgr);

  if (v->input.options.focus_follows_mouse) {
    if (sgr.event_type & mouse_move && sgr.button_state == mouse_none) {
      for (size_t i = 0; i < v->scene.hosts.length; i++) {
        struct pty_host *h = vec_nth(&v->scene.hosts, i);
        struct bounds b = h->rect.window;
        if (b.x <= sgr.column && (b.x + b.columns) >= sgr.column && b.y <= sgr.row && (b.y + b.lines) >= sgr.row) {
          velvet_scene_set_focus(&v->scene, i);
          break;
        }
      }
    }
  }

  struct pty_host *target = coord_to_client(v, sgr);
  if (!target) return;

  struct mouse_options m = target->emulator.options.mouse;
  if (m.tracking == MOUSE_TRACKING_OFF || m.tracking == MOUSE_TRACKING_LEGACY) return;
  if (m.mode != MOUSE_MODE_SGR) return;

  bool do_send =
      (m.tracking == MOUSE_TRACKING_ALL_MOTION) ||
      ((m.tracking == MOUSE_TRACKING_CLICK || m.tracking == MOUSE_TRACKING_CELL_MOTION) &&
       sgr.event_type == mouse_click) ||
      (m.tracking == MOUSE_TRACKING_CELL_MOTION && sgr.event_type == mouse_move && sgr.button_state != mouse_none);
  // TODO: scroll wheel support
  if (do_send) {
    send_mouse_sgr(target, sgr);
  }
}

static void send_bracketed_paste(struct velvet *v) {
  struct velvet_input *in = &v->input;
  struct pty_host *focus = vec_nth(&v->scene.hosts, v->scene.focus);
  bool enclose = focus->emulator.options.bracketed_paste;
  uint8_t *start = in->command_buffer.content;
  size_t len = in->command_buffer.len;
  if (!enclose) {
    start += bracketed_paste_start.len;
    len -= bracketed_paste_start.len + bracketed_paste_end.len;
  }
  struct u8_slice s = {.content = start, .len = len};
  string_push_slice(&focus->emulator.pending_input, s);
  string_clear(&v->input.command_buffer);
  in->state = VELVET_INPUT_STATE_NORMAL;
}

static void dispatch_csi(struct velvet *v, uint8_t ch) {
  struct velvet_input *in = &v->input;
  string_push_char(&v->input.command_buffer, ch);

  if (string_starts_with(&v->input.command_buffer, bracketed_paste_start)) {
    if (string_ends_with(&v->input.command_buffer, bracketed_paste_end)) {
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
    if (!v->scene.hosts.length) return;
    struct csi c = {0};
    struct u8_slice s = string_range(&v->input.command_buffer, 2, -1);
    size_t len = csi_parse(&c, s);
    if (c.state == CSI_ACCEPT) {
      assert(len == s.len);

#define KEY(leading, intermediate, final)                                                                              \
  ((((uint32_t)leading) << 16) | (((uint32_t)intermediate) << 8) | (((uint32_t) final)))

      switch (KEY(c.leading, c.intermediate, c.final)) {
#define CSI(l, i, f, fn, _)                                                                                            \
  case KEY(l, i, f): DISPATCH_##fn(v, &c); break;
#include "input_csi.def"
      default: send_csi_todo(v); break;
      }
    }
    string_clear(&v->input.command_buffer);
    in->state = VELVET_INPUT_STATE_NORMAL;
  }
}

static void dispatch_tree_recursive(struct velvet *v, struct velvet_keymap *leaf, struct velvet_keymap *end_node) {
  if (leaf == end_node) return;
  dispatch_tree_recursive(v, leaf->parent, end_node);
  dispatch_key_event(v, leaf->key);
}

void velvet_input_unwind(struct velvet *v) {
  struct velvet_keymap *k, *current, *root;
  current = v->input.keymap;
  root = current->root;
  // key not matched. Unwind the parent tree until we find a mapping to execute,
  // and then replay the unmatched keys. Typically this means replaying
  // the keys in the root mapping (e.g. inserting the characters),
  // but it can also mean resolving a mapping which is a prefix of a longer mapping.
  for (k = current; k && k != root; k = k->parent) {
    if (k->on_key) {
      k->on_key(k, k->key);
      break;
    }

    // Ensure at least one key is consumed before reinsertion.
    if (k->parent == root) {
      root->on_key(root, k->key);
      break;
    }
  }

  // now replay the unresolved keys in the root mapping
  v->input.keymap = root;
  dispatch_tree_recursive(v, current, k);
}

void input_unwind_callback(void *data) {
  struct velvet *v = data;
  velvet_input_unwind(v);
}

static bool keymap_has_mapping(struct velvet_keymap *k) {
  struct velvet_keymap *root = k->root;
  for (; k && k != root; k = k->parent) {
    if (k->on_key) return true;
  }
  return false;
}

/* TODO: How should meta/alt behave */ 
static bool key_event_equals(struct velvet_key_event k1, struct velvet_key_event k2) {
  if (k1.modifiers != k2.modifiers) return false;
  if (k1.key.literal != k2.key.literal) return false;
  if (k1.key.literal) return k1.key.symbol.numeric == k2.key.symbol.numeric;
  return strcmp(k1.key.special.name, k2.key.special.name) == 0;
}

// this is supposed to emulate VIM-like behavior
static void dispatch_key_event(struct velvet *v, struct velvet_key_event key) {
  assert(v);
  assert(v->input.keymap);
  struct velvet_keymap *k, *root, *current;
  current = v->input.keymap;
  root = current->root;
  assert(root);

  // cancel any scheduled unwind
  struct io_schedule *unwind;
  vec_find(unwind, v->event_loop.scheduled_actions, unwind->callback == input_unwind_callback);
  if (unwind) vec_remove(&v->event_loop.scheduled_actions, unwind);

  // First check if this key matches a keybind in the current keymap
  for (k = current->first_child; k; k = k->next_sibling) {
    if (key_event_equals(k->key, key)) {
      if (k->first_child) {
        v->input.keymap = k;
        // If this sequence has both a mapping and a continuation,
        // defer the mapping until the intended sequence can be determined.
        if (keymap_has_mapping(k))
          io_schedule(&v->event_loop, v->input.options.key_chain_timeout_ms, input_unwind_callback, v);
      } else {
        // this choord is terminal so on_key must be set
        assert(k->on_key);
        // deliberately reset the keymap before invoking the mapping
        // this gives the mapping the opportunity to modify the keymap.
        v->input.keymap = k->root;
        k->on_key(k, key);
      }
      return;
    }
  }

  // ESC cancels any pending keybind
  if (key.key.literal && key.key.symbol.numeric == ESC && current != root) {
    v->input.keymap = root;
    return;
  }

  if (current == root) {
    root->on_key(root, key);
  } else {
    // Since the current key was not matched, we should match the longest
    // prefix and then reinsert all pending keys
    velvet_input_unwind(v);
    dispatch_key_event(v, key);
  }
}

static void dispatch_app(struct velvet *v, uint8_t ch) {
  bool found = false;
  string_push_char(&v->input.command_buffer, ch);

  struct u8_slice key = string_as_u8_slice(v->input.command_buffer);
  for (int i = 0; !found && i < LENGTH(keys); i++) {
    struct u8_slice key_escape = u8_slice_from_cstr(keys[i].escape);
    if (u8_slice_equals(key, key_escape)) {
      struct velvet_key_event e = { .key.special = keys[i] };
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
    struct velvet_key_event k = key_event_from_byte(ch);
    /* ALT and META are different, but in VT environments they have historically
     * been collated. Since there is no way to distinguish them,
     * treat ESC as both. */
    k.modifiers |= MODIFIER_ALT;
    k.modifiers |= MODIFIER_META;
    dispatch_key_event(v, k);
  }
}

static void velvet_input_send_literal(struct velvet *v, struct utf8 s, enum velvey_key_modifier m) {
  if (s.numeric == ESC) {
    send_byte(v, ESC);
  } else {
    bool iscntrl = m & MODIFIER_CTRL;
    bool is_meta = (m & MODIFIER_ALT) || (m & MODIFIER_META);
    if (is_meta) send_byte(v, ESC);

    if (iscntrl && s.numeric == ' ') send_byte(v, 0);
    else for (int i = 0; i < 4 && s.utf8[i]; i++)
      send_byte(v, s.utf8[i] & (iscntrl ? 0x1f : 0xff));
  }
}

static void velvet_input_send_special(struct velvet *v, struct special_key s, enum velvey_key_modifier m) {
  /* TODO: Modifiers + respect client keyboard settings */
  for (char *ch = s.escape; *ch; ch++)
    send_byte(v, *ch);
}

void velvet_input_send(struct velvet_keymap *k, struct velvet_key_event e) {
  // TODO: check keyboard settings of recipient and modify event accordingly
  struct velvet *v = k->data;
  if (e.key.literal) velvet_input_send_literal(v, e.key.symbol, e.modifiers);
  else velvet_input_send_special(v, e.key.special, e.modifiers);
  
}

static void dispatch_normal(struct velvet *v, uint8_t ch) {
  struct velvet_input *in = &v->input;
  assert(in->command_buffer.len == 0);

  if (ch == ESC) {
    string_push_char(&v->input.command_buffer, ch);
    in->state = VELVET_INPUT_STATE_ESC;
    return;
  }

  struct velvet_key_event key = key_event_from_byte(ch);
  dispatch_key_event(v, key);
  assert(v->input.keymap);
}

void velvet_process_input(struct velvet *v, struct u8_slice str) {
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
    struct velvet_key_event k = {.key.symbol.numeric = ESC, .key.literal = true };
    dispatch_key_event(v, k);
  }
}

static void dispatch_focus(struct velvet *v, const struct csi *const c) {
  struct pty_host *focus = vec_nth(&v->scene.hosts, v->scene.focus);
  if (focus->emulator.options.focus_reporting) send(v, c->final == 'O' ? vt_focus_out : vt_focus_in);
}

void DISPATCH_FOCUS_OUT(struct velvet *v, const struct csi *const c) {
  dispatch_focus(v, c);
}
void DISPATCH_FOCUS_IN(struct velvet *v, const struct csi *const c) {
  dispatch_focus(v, c);
}
void DISPATCH_SGR_MOUSE(struct velvet *v, const struct csi *const c) {
  send_csi_mouse(v, c);
}
static void dispatch_arrow_key(struct velvet *v, const struct csi *const c) {
  struct pty_host *focus = vec_nth(&v->scene.hosts, v->scene.focus);
  if (focus->emulator.options.application_mode) {
    send_bytes(v, ESC, 'O', c->final);
  } else {
    send(v, string_as_u8_slice(v->input.command_buffer));
  }
}
void DISPATCH_ARROW_KEY_UP(struct velvet *v, const struct csi *const c) {
  dispatch_arrow_key(v, c);
}
void DISPATCH_ARROW_KEY_DOWN(struct velvet *v, const struct csi *const c) {
  dispatch_arrow_key(v, c);
}
void DISPATCH_ARROW_KEY_LEFT(struct velvet *v, const struct csi *const c) {
  dispatch_arrow_key(v, c);
}
void DISPATCH_ARROW_KEY_RIGHT(struct velvet *v, const struct csi *const c) {
  dispatch_arrow_key(v, c);
}

void velvet_input_destroy(struct velvet_input *in) {
  string_destroy(&in->command_buffer);
}

static void velvet_keymap_remove_internal(struct velvet_keymap *k) {
  if (k->on_key) k->on_key(k, (struct velvet_key_event){.removed = true});

  // If the kd mapping has continuations, we need to keep the node in the tree.
  // However, we clear the associated action and data
  if (k->first_child) {
    k->on_key = nullptr;
    k->data = nullptr;
    return;
  }

  struct velvet_keymap *p = k->parent;
  if (p->first_child == k) {
    p->first_child = k->next_sibling;
  } else {
    struct velvet_keymap *pre = p->first_child;
    for (; pre->next_sibling != k; pre = pre->next_sibling);
    assert(pre);
    pre->next_sibling = k->next_sibling;
  }
  if (!p->on_key && !p->first_child) {
    velvet_keymap_remove_internal(p);
  }
}

static bool key_from_slice(struct u8_slice s, struct velvet_key *result) {
  struct velvet_key k = {0};
  assert(s.len > 0);

  if (s.content[0] >= 0xC2 && s.content[0] < 0xF4) {
    // utf8
    size_t expected_length = utf8_expected_length(s.content[0]);
    if (expected_length != s.len) {
      velvet_log("Unexpected utf8 length: %.*s", (int)s.len, s.content);
    }
    k.literal = true;
    for (size_t i = 0; i < s.len; i++) k.symbol.utf8[i] = s.content[i];
    *result = k;
    return true;
  } else if (s.len == 1) {
    k.literal = true;
    k.symbol.utf8[0] = s.content[0];
    *result = k;
    return true;
  }
  for (int i = 0; i < LENGTH(keys); i++) {
    struct u8_slice special = u8_slice_from_cstr(keys[i].name);
    if (u8_slice_equals_ignore_case(s, special)) {
      k.literal = false;
      k.special = keys[i];
      *result = k;
      return true;
    }
  }
  return false;
}

#define MAX_KEYS 10
int keymap_parse_keys(struct u8_slice keys, struct velvet_key_event *events, int n_events) {
  int count = 0;
  struct velvet_key_event k = {0};
  for (size_t i = 0; i < keys.len; i++) {
    uint8_t ch = keys.content[i];

    // '<' indicates a modifiers + key combo such as <C-S-M-w>, or a named key, such as
    // <space>, <prefix>, <leader>, or other user defined named key
    // If no known macro or modifiers are detected, we treat the sequence as a raw key sequence
    // (vim-like behavior). So <asd will match the literal sequence {<, a, s, d}
    if (ch == '<') {
      uint32_t mods = 0;
      size_t j = i + 1;
      for (; j < keys.len && keys.content[j] != '>'; j++);
      struct u8_slice inner = {.content = keys.content + i + 1, .len = j - i - 1};
      if (inner.len > 0 && j < keys.len && keys.content[j] == '>') {
        char *modifiers[] = {
            "S-",    // shift     -- 1 << 0
            "M-",    // alt       -- 1 << 1
            "C-",    // ctrl      -- 1 << 2
            "D-",    // super     -- 1 << 3
            "H-",    // hyper     -- 1 << 4
            "T-",    // meta      -- 1 << 5
            "Caps-", // caps lock -- 1 << 6
            "Num-",  // num lock  -- 1 << 7
        };
        struct u8_slice modifier_slices[LENGTH(modifiers)] = {0};
        for (int i = 0; i < LENGTH(modifiers); i++) modifier_slices[i] = u8_slice_from_cstr(modifiers[i]);

        bool changed = true;
        while (changed) {
          changed = false;
          for (int i = 0; i < LENGTH(modifiers); i++) {
            struct u8_slice mod = modifier_slices[i];
            if (u8_slice_starts_with(inner, mod)) {
              inner = u8_slice_range(inner, mod.len, -1);
              mods |= (1 << i);
              changed = true;
            }
          }
        }
        struct velvet_key key = {0};
        if (key_from_slice(inner, &key)){
          k.key = key;
          k.modifiers = mods;
          if (count < n_events) {
            events[count] = k;
          }
          count++;
          k = (struct velvet_key_event){0};
          i = j;
          continue;
        } else {
          velvet_log("Unrecognized key %.*s", (int)inner.len, inner.content);
        }
      }
    }

    k.key.literal = true;
    k.key.symbol.numeric = ch;
    if (count < n_events) {
      events[count] = k;
    }
    count++;
    k = (struct velvet_key_event){0};
  }
  if (count >= n_events) {
    velvet_log("Mapping '%.*s' rejected; the maximum allowed sequence is %d (was %d)", (int)keys.len, keys.content, MAX_KEYS, count);
    return -1;
  }
  return count;
}

void velvet_keymap_unmap(struct velvet_keymap *root, struct u8_slice key_sequence) {
  int n_keys;
  struct velvet_key_event keys[MAX_KEYS] = {0};
  n_keys = keymap_parse_keys(key_sequence, keys, MAX_KEYS);
  if (n_keys < 1) return;
  for (int i = 0; root && i < n_keys; i++) {
    for (root = root->first_child; root && !key_event_equals(root->key, keys[i]); root = root->next_sibling);
  }
  if (root) velvet_keymap_remove_internal(root);
}

struct velvet_keymap *velvet_keymap_map(struct velvet_keymap *root, struct u8_slice key_sequence) {
  int n_keys;
  struct velvet_key_event keys[MAX_KEYS] = {0};
  struct velvet_keymap *prev, *chain, *parent;
  assert(root);
  assert(key_sequence.len);

  n_keys = keymap_parse_keys(key_sequence, keys, MAX_KEYS);
  if (n_keys < 1) return nullptr;

  parent = root;
  for (int i = 0; i < n_keys; i++) {
    struct velvet_key_event k = keys[i];
    prev = nullptr;
    for (chain = parent->first_child; chain && !key_event_equals(chain->key, k); prev = chain, chain = chain->next_sibling);
    if (!chain) {
      chain = velvet_calloc(1, sizeof(*parent));
      chain->parent = parent;
      chain->data = nullptr;
      chain->key = k;
      chain->root = root;
      if (prev) {
        chain->next_sibling = prev->next_sibling;
        prev->next_sibling = chain;
      } else {
        parent->first_child = chain;
      }
    }
    parent = chain;
  }
  if (parent->on_key) {
    struct velvet_key_event removed = { .removed = true };
    parent->on_key(parent, removed);
  }
  parent->root = root->root ? root->root : root;
  return parent;
}
