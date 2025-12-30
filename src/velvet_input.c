#include "csi.h"
#include "utils.h"
#include "virtual_terminal_sequences.h"
#include "velvet.h"
#include <string.h>
#include "velvet_keyboard.h"

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
enum mouse_trigger { mouse_down = 1, mouse_up = 2 };

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
  assert(v->scene.windows.length > 0);
  struct velvet_window *focus = velvet_scene_get_focus(&v->scene);
  string_push_slice(&focus->emulator.pending_input, s);
}

static void send_byte(struct velvet *v, uint8_t ch) {
  send_bytes(v, ch);
}

static void dispatch_key_event(struct velvet *v, struct velvet_key_event key);
static struct velvet_key_event key_event_from_byte(uint8_t ch) {
  // special case for <C-Space>
  if (ch == 0)
    return (struct velvet_key_event){.key.literal = true, .key.symbol = ' ', .modifiers = MODIFIER_CTRL};

  for (int i = 0; i < LENGTH(keys); i++) {
    struct special_key k = keys[i];
    struct velvet_key_event special = {0};
    struct u8_slice single = { .content = &ch, .len = 1 };
    if (u8_slice_equals(single, u8_slice_from_cstr(k.escape))) {
      special.key.literal = false;
      special.key.special = k;
      return special;
    }
  }

  struct velvet_key_event k = {0};
  bool iscntrl = CTRL(ch) == ch;
  if (iscntrl) {
    ch = ch + 96;
  }

  bool isshift = ch >= 'A' && ch <= 'Z';
  if (!isshift) {
  // TODO: locale aware shift table
  bool shift_table[] = {
      ['!'] = '1', ['@'] = '2', ['#'] = '3', ['$'] = '4',
      ['%'] = '5', ['^'] = '6', ['&'] = '7', ['*'] = '8',
      ['('] = '9', [')'] = '0', ['<'] = ',', ['>'] = '.',
      [':'] = ';', ['"'] = '\'', ['|'] = '\\', ['~'] = '`',
      ['?'] = '/', ['{'] = '[', ['}'] = ']',
  };
  isshift = (ch < LENGTH(shift_table)) && shift_table[ch];
  }

  k.key.symbol = ch;
  k.key.literal = true;
  k.modifiers = ((iscntrl * MODIFIER_CTRL) | (isshift * MODIFIER_SHIFT));
  return k;
}

static void send_csi_todo(struct velvet *v) {
  struct velvet_input *in = &v->input;
  TODO("Input CSI: %.*s", (int)in->command_buffer.len, in->command_buffer.content);
  string_clear(&v->input.command_buffer);
}

static void DISPATCH_KITTY_KEY(struct velvet *v, struct csi c);
static void DISPATCH_FOCUS_OUT(struct velvet *v, struct csi c);
static void DISPATCH_FOCUS_IN(struct velvet *v, struct csi c);
static void DISPATCH_SGR_MOUSE(struct velvet *v, struct csi c);

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

static struct velvet_window *coord_to_client(struct velvet *v, struct mouse_sgr sgr) {
  /* convert to 0-index */
  sgr.column--;
  sgr.row--;
  struct velvet_window *h;

  for (enum velvet_scene_layer layer = VELVET_LAYER_LAST - 1; layer >= VELVET_LAYER_TILED; layer--) {
    vec_where(h, v->scene.windows, h->layer == layer) {
      if (h->dragging) continue;
      struct rect b = h->rect.client;
      if (b.x <= sgr.column && (b.x + b.w) > sgr.column && b.y <= sgr.row && (b.y + b.h) > sgr.row) {
        return h;
      }
    }
  }

  return nullptr;
}

static bool between(int value, int lo, int hi) {
  return value >= lo && value <= hi;
}

static struct velvet_window *coord_to_title(struct velvet *v, struct mouse_sgr sgr) {
  /* convert to 0-index */
  sgr.column--;
  sgr.row--;
  struct velvet_window *h;
  /* first try to match the title bar */
  for (enum velvet_scene_layer layer = VELVET_LAYER_LAST; layer > 0; layer--) {
    vec_where(h, v->scene.windows, h->layer == layer) {
      if (h->dragging) continue;
      struct rect b = h->rect.window;
      int bw = h->border_width;
      if (!bw) continue;
      if (between(sgr.column, b.x + bw, b.x + b.w - bw - 1) /* columns match ? */
          && sgr.row == b.y + bw - 1 /* line matches ? */) {
        return h;
      }
    }
  }
  return nullptr;
}

// TODO: this
static bool mouse_overlaps_window(struct velvet_window *w, struct mouse_sgr sgr) { return false; }
static bool mouse_overlaps_client(struct velvet_window *w, struct mouse_sgr sgr) { return false; }
static bool mouse_overlaps_title(struct velvet_window *w, struct mouse_sgr sgr) { return false; }

static struct velvet_window *coord_to_window(struct velvet *v, struct mouse_sgr sgr, enum velvet_scene_layer layer) {
  /* convert to 0-index */
  sgr.column--;
  sgr.row--;
  struct velvet_window *h;

  /* then fall back to full window */
  vec_rforeach(h, v->scene.windows) {
    if (h->dragging) continue;
    if (layer && h->layer != layer) continue;
    struct rect b = h->rect.window;
    if (b.x <= sgr.column && (b.x + b.w) > sgr.column && b.y <= sgr.row && (b.y + b.h) > sgr.row) {
      return h;
    }
  }
  return nullptr;
}

static struct mouse_sgr mouse_sgr_from_csi(struct csi c) {
  int btn = c.params[0].primary;
  int col = c.params[1].primary;
  int row = c.params[2].primary;

  enum mouse_state mouse_button = btn & 0x03;
  enum mouse_modifiers modifiers = btn & (0x04 | 0x08 | 0x10);
  enum mouse_event event_type = btn & (0x20 | 0x40);

  struct mouse_sgr sgr = {
      .button_state = mouse_button,
      .modifiers = modifiers,
      .event_type = event_type,
      .row = row,
      .column = col,
      .trigger = c.final == 'M' ? mouse_down : mouse_up,
  };
  return sgr;
}

static void send_mouse_sgr(struct velvet_window *target, struct mouse_sgr trans) {
  int btn = trans.button_state | trans.modifiers | trans.event_type;
  int start = target->emulator.pending_input.len;
  if (target->emulator.options.mouse.mode == MOUSE_MODE_SGR) {
    string_push_csi(&target->emulator.pending_input,
                    '<',
                    INT_SLICE(btn, trans.column, trans.row),
                    trans.trigger == mouse_down ? "M" : "m");
    int end = target->emulator.pending_input.len;
    struct u8_slice s = string_range(&target->emulator.pending_input, start, end);
    velvet_log("send sgr: %.*s", (int)s.len, s.content);
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

static void velvet_input_send_special(struct velvet *v, struct special_key s, enum velvey_key_modifier m);

static bool get_special_key_by_name(char *name, struct special_key *key) {
  for (int i = 0; i < LENGTH(keys); i++) {
    struct special_key k = keys[i];
    if (strcmp(name, k.name) == 0) {
      *key = k;
      return true;
    }
  }
  return true;
}

static void send_csi_mouse(struct velvet *v, struct csi c) {
  struct velvet_input *in = &v->input;
  struct mouse_sgr sgr = mouse_sgr_from_csi(c);

  // mouse_debug_logging(sgr);
  struct velvet_window *target = nullptr;
  if (in->dragging.id) {
    vec_find(target, v->scene.windows, target->pty == in->dragging.id);
    if (!target) {
      in->dragging = (struct velvet_input_drag_event) {0};
    }
  }

  if (in->dragging.id || ((target = coord_to_title(v, sgr)) && (sgr.event_type == mouse_click))) {
    if (sgr.event_type == mouse_click && sgr.trigger == mouse_down) {
      in->dragging = (struct velvet_input_drag_event){
          .id = target->pty,
          .drag_start.x = sgr.column,
          .drag_start.y = sgr.row,
          .drag_start.client_x = target->rect.window.x,
          .drag_start.client_y = target->rect.window.y,
      };
      target->dragging = true;
      target->layer = VELVET_LAYER_FLOATING;
      velvet_scene_set_focus(&v->scene, vec_index(&v->scene.windows, target));
    } else if (sgr.event_type == mouse_click && sgr.trigger == mouse_up) {
      /* drop */
      target->dragging = false;
      in->dragging = (struct velvet_input_drag_event) {0};
      if (sgr.modifiers & modifier_alt) {
        struct velvet_window *drop_target = coord_to_window(v, sgr, VELVET_LAYER_TILED);
        if (drop_target && drop_target != target) {
          target->layer = VELVET_LAYER_TILED;
          bool above /* otherwise below */ = (sgr.row - drop_target->rect.window.y) < (drop_target->rect.window.h / 2);
          int target_index = vec_index(&v->scene.windows, target);
          int drop_index = vec_index(&v->scene.windows, drop_target);

          /* account for the fact that we temporarily remove the window */
          if (target_index < drop_index) drop_index--;
          if (!above) drop_index++;
          struct velvet_window t = *target;
          vec_remove(&v->scene.windows, target);
          vec_insert(&v->scene.windows, drop_index, &t);
          velvet_scene_set_focus(&v->scene, drop_index);
        } else {
          target->layer = VELVET_LAYER_TILED;
        }
      }
    } else if (in->dragging.id && (sgr.event_type & mouse_move)) {
      int delta_x = sgr.column - in->dragging.drag_start.x;
      int delta_y =  sgr.row - in->dragging.drag_start.y;
      int adj_x = target->rect.window.x - in->dragging.drag_start.client_x;
      int adj_y = target->rect.window.y - in->dragging.drag_start.client_y;
      delta_x -= adj_x;
      delta_y -= adj_y;
      target->rect.window.x += delta_x;
      target->rect.client.x += delta_x;
      target->rect.window.y += delta_y;
      target->rect.client.y += delta_y;
      velvet_log("Border drag: %d,%d", delta_x, delta_y);
    }
    return;
  }

  target = coord_to_title(v, sgr);
  if (!target) target = coord_to_client(v, sgr);
  if (!target) return;

  if ((v->input.options.focus_follows_mouse || sgr.event_type == mouse_click) && target) {
    velvet_scene_set_focus(&v->scene, vec_index(&v->scene.windows, target));
  }

  struct mouse_sgr trans = sgr;
  trans.row = sgr.row - target->rect.client.y;
  trans.column = sgr.column - target->rect.client.x;

  struct mouse_options m = target->emulator.options.mouse;

  bool do_send =
      (m.tracking == MOUSE_TRACKING_ALL_MOTION) ||
      ((m.tracking == MOUSE_TRACKING_CLICK || m.tracking == MOUSE_TRACKING_CELL_MOTION) &&
       sgr.event_type == mouse_click) ||
      (m.tracking == MOUSE_TRACKING_CELL_MOTION && sgr.event_type == mouse_move && sgr.button_state != mouse_none) ||
      (m.tracking && sgr.event_type == mouse_scroll);

  if (!(trans.row > 0 && trans.column > 0 && trans.column <= target->rect.client.w &&
        trans.row <= target->rect.client.h)) {
    /* verify the event is actually within the client area */
    do_send = false;
  }

  if (do_send) {
    send_mouse_sgr(target, trans);
  } else if (sgr.event_type == mouse_scroll && target->emulator.options.alternate_screen) {
    struct special_key k = {0};
    /* In the absence of mouse tracking,
     * scroll up/down is treated as arrow keys in the alternate screen
     * Deliberately don't process this key in any keymaps.
     * Mappings related to scrolling should be handled by sequences such as '<S-M-ScrollWheelUp>'
     */
    if (sgr.scroll_direction == scroll_up) {
      get_special_key_by_name("SS3_UP", &k);
      velvet_input_send_special(v, k, 0);
    } else if (sgr.scroll_direction == scroll_down) {
      get_special_key_by_name("SS3_DOWN", &k);
      velvet_input_send_special(v, k, 0);
    }
  } else if (sgr.event_type == mouse_scroll && !target->emulator.options.alternate_screen) {
    struct screen *screen = vte_get_current_screen(&target->emulator);
    /* in the primary screen, scrolling affects the current view */
    int current_offset = screen_get_scroll_offset(screen);
    if (sgr.scroll_direction == scroll_up) {
      int num_lines = screen_get_scroll_height(screen);
      screen_set_scroll_offset(screen, MIN(num_lines, current_offset + 1));
    } else if (sgr.scroll_direction == scroll_down) {
      screen_set_scroll_offset(screen, MAX(0, current_offset - 1));
    }
  }
}

static void scroll_to_bottom(struct velvet *v) {
  struct velvet_window *focus = velvet_scene_get_focus(&v->scene);
  if (focus) screen_set_scroll_offset(&focus->emulator.primary, 0);
}

static void send_bracketed_paste(struct velvet *v) {
  struct velvet_input *in = &v->input;
  struct velvet_window *focus = velvet_scene_get_focus(&v->scene);
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
  scroll_to_bottom(v);
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
    if (!v->scene.windows.length) return;
    {
      struct u8_slice raw = string_as_u8_slice(v->input.command_buffer);
      for (int i = 0; i < LENGTH(keys); i++) {
        struct special_key k = keys[i];
        if (u8_slice_equals(raw, u8_slice_from_cstr(k.escape))) {
          string_clear(&v->input.command_buffer);
          in->state = VELVET_INPUT_STATE_NORMAL;
          struct velvet_key_event evt = {.key.special = k};
          dispatch_key_event(v, evt);
          return;
        }
      }
    }
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
  v->input.unwind_callback_token = 0;
}

static bool keymap_has_mapping(struct velvet_keymap *k) {
  struct velvet_keymap *root = k->root;
  for (; k && k != root; k = k->parent) {
    if (k->on_key) return true;
  }
  return false;
}

static struct velvet_key_event key_normalize(struct velvet_key_event key) {
  /* just treat alt the same as meta for all practical purposes */
  if ((key.modifiers & MODIFIER_ALT) || (key.modifiers & MODIFIER_META)) {
    key.modifiers &= ~MODIFIER_ALT;
    key.modifiers |= MODIFIER_META;
  }
  if (key.modifiers & MODIFIER_SHIFT) {
    if (key.key.literal) {
      char ch = key.key.symbol;
      if (ch >= 'A' && ch <= 'Z') ch += 32;
      key.key.symbol = ch;
    }
  }
  return key;
}

/* TODO: How should meta/alt behave */ 
static bool key_event_equals(struct velvet_key_event k1, struct velvet_key_event k2) {
  k1 = key_normalize(k1);
  k2 = key_normalize(k2);
  if (k1.modifiers != k2.modifiers) return false;
  if (k1.key.literal != k2.key.literal) return false;
  if (k1.key.literal) return k1.key.symbol == k2.key.symbol;
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
        if (keymap_has_mapping(k)) {
          if (v->input.unwind_callback_token) {
            io_schedule_cancel(&v->event_loop, v->input.unwind_callback_token);
          }
          v->input.unwind_callback_token =
              io_schedule(&v->event_loop, v->input.options.key_chain_timeout_ms, input_unwind_callback, v);
        }
      } else {
        // this choord is terminal so on_key must be set
        assert(k->on_key);

        /* If the current keymap is still active because of a repeat,
         * only trigger a continuation if it allows repeating. */
        if (v->input.last_repeat && !k->is_repeatable) break;

        uint64_t now = get_ms_since_startup();
        uint64_t last_repeat = v->input.last_repeat;
        if (!last_repeat) last_repeat = now;
        uint64_t timeout = v->input.options.key_repeat_timeout_ms;

        /* if `timeout` has elapsed since the previous repeat, break */
        if (k->is_repeatable && now - last_repeat > timeout) break;

        /* update the repeat timer */
        if (k->is_repeatable) v->input.last_repeat = now;

        k->on_key(k, key);

        /* reset the keymap unless the key is repeatable */
        if (!k->is_repeatable) v->input.keymap = k->root;
      }
      return;
    }
  }

  bool did_repeat = v->input.last_repeat > 0;
  v->input.last_repeat = 0;

  // ESC cancels any pending keybind
  if (key.key.literal && key.key.symbol == ESC && current != root) {
    v->input.keymap = root;
    return;
  }

  if (current == root) {
    root->on_key(root, key);
  } else if (did_repeat) {
    /* don't unwind the unresolved stack if we are handling a repeat timeout */
    v->input.keymap = root;
    dispatch_key_event(v, key);
  } else {
    // Since the current key was not matched, we should match the longest
    // prefix and then reinsert all pending keys
    velvet_input_unwind(v);
    dispatch_key_event(v, key);
  }
}

static void DISPATCH_KITTY_KEY(struct velvet *v, struct csi c) {
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

static void velvet_input_send_literal(struct velvet *v, char s, enum velvey_key_modifier m) {
  if (s == ESC) {
    send_byte(v, ESC);
  } else {
    bool iscntrl = m & MODIFIER_CTRL;
    bool is_meta = (m & MODIFIER_ALT) || (m & MODIFIER_META);
    if (is_meta) send_byte(v, ESC);

    if (iscntrl && s == ' ') send_byte(v, 0);
    if (iscntrl) send_byte(v, s & 0x1f);
    else send_byte(v, s);
  }
}

static void velvet_input_send_special(struct velvet *v, struct special_key s, enum velvey_key_modifier m) {
  /* TODO: Modifiers + respect client keyboard settings */
  for (char *ch = s.escape; *ch; ch++) {
    send_byte(v, *ch);
  }
}

void velvet_input_send(struct velvet_keymap *k, struct velvet_key_event e) {
  if (e.removed) return;
  // TODO: check keyboard settings of recipient and modify event accordingly
  struct velvet *v = k->data;
  scroll_to_bottom(v);
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
    struct velvet_key_event k = { 0 };
    get_special_key_by_name("ESC", &k.key.special);
    dispatch_key_event(v, k);
  }
}

static void dispatch_focus(struct velvet *v, struct csi c) {
  struct velvet_window *focus = velvet_scene_get_focus(&v->scene);
  if (focus->emulator.options.focus_reporting) send(v, c.final == 'O' ? vt_focus_out : vt_focus_in);
  if (c.final == 'I' && v->input.input_socket)
    v->focused_socket = v->input.input_socket;
}

void DISPATCH_FOCUS_OUT(struct velvet *v, struct csi c) {
  dispatch_focus(v, c);
}
void DISPATCH_FOCUS_IN(struct velvet *v, struct csi c) {
  dispatch_focus(v, c);
}
void DISPATCH_SGR_MOUSE(struct velvet *v, struct csi c) {
  send_csi_mouse(v, c);
}

static void velvet_keymap_remove_internal(struct velvet_keymap *const k) {
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
  free(k);
}

static void velvet_keymap_destroy(struct velvet_keymap *k) {
  struct velvet_keymap *chld = k->first_child;
  for (; chld; ) {
    chld->parent = nullptr;
    chld->root = nullptr;
    struct velvet_keymap *next = chld->next_sibling;
    velvet_keymap_destroy(chld);
    chld = next;
  }
  /* the remove event is needed to free resources associated with the event */
  if (k->on_key) k->on_key(k, (struct velvet_key_event){.removed = true});
  free(k);
}

void velvet_input_destroy(struct velvet_input *in) {
  string_destroy(&in->command_buffer);
  velvet_keymap_destroy(in->keymap->root);
}

static bool key_from_slice(struct u8_slice s, struct velvet_key *result) {
  struct velvet_key k = {0};
  assert(s.len > 0);

  if (s.len == 1) {
    k = key_event_from_byte(s.content[0]).key;
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

struct velvet_key_iterator {
  struct u8_slice src;
  size_t cursor;
  struct velvet_key_event current;
  struct u8_slice current_range;
  bool invalid;
};

static bool is_whitespace(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

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
    it->current = key_event_from_byte(ch);
    it->cursor = cursor + 1;
    it->current_range = u8_slice_range(t, cursor, cursor + 1);
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

void velvet_keymap_unmap(struct velvet_keymap *root, struct u8_slice key_sequence) {
  struct velvet_key_iterator it = { .src = key_sequence };
  struct velvet_keymap *to_remove = root;
  for (; velvet_key_iterator_next(&it); ) {
    for (to_remove = to_remove->first_child; to_remove && !key_event_equals(to_remove->key, it.current); to_remove = to_remove->next_sibling);
  }
  if (to_remove) velvet_keymap_remove_internal(to_remove);
}

struct velvet_keymap *velvet_keymap_map(struct velvet_keymap *root, struct u8_slice key_sequence) {
  struct velvet_keymap *prev, *chain, *parent;
  assert(root);
  assert(key_sequence.len);

  struct velvet_key_iterator it = { .src = key_sequence };
  struct velvet_key_iterator test = it;
  for (; velvet_key_iterator_next(&test);) {
    if (test.invalid) {
      velvet_log("Rejecting keymap %.*s: The key %.*s was not recognized.",
                 (int)key_sequence.len,
                 key_sequence.content,
                 (int)test.current_range.len,
                 test.current_range.content);
    }
  }


  parent = root;
  for (; velvet_key_iterator_next(&it); ) {
    struct velvet_key_event k = it.current;
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

void velvet_input_put(struct velvet *in, struct u8_slice str) {
  struct velvet_key_iterator it = { .src = str };
  struct velvet_key_iterator copy = it;
  for (; velvet_key_iterator_next(&copy); ) {
    if (it.invalid) {
      velvet_log("Put: rejecting invalid sequence %.*s", (int)str.len, str.content);
    }
  }
  struct velvet_keymap *root = in->input.keymap->root;
  for (; velvet_key_iterator_next(&it); ) {
    velvet_input_send(root, it.current);
  }
}
