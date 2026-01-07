#include "csi.h"
#include "utils.h"
#include "virtual_terminal_sequences.h"
#include "velvet.h"
#include <string.h>
#include "velvet_keyboard.h"
#include "utf8proc/utf8proc.h"

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
  struct velvet_window *focus = velvet_scene_get_focus(&v->scene);
  if (focus) string_push_slice(&focus->emulator.pending_input, s);
}

static void send_byte(struct velvet *v, uint8_t ch) {
  send_bytes(v, ch);
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

static void dispatch_key_event(struct velvet *v, struct velvet_key_event key);

static struct velvet_key_event key_event_from_codepoint(uint32_t cp) {
  // special case for <C-Space>
  if (cp == 0) {
    struct velvet_key_event e = { .modifiers = MODIFIER_CTRL };
    find_key("space", &e.key);
    return e;
  }

  struct velvet_key_event k = {0};

         
  bool iscntrl = CTRL(cp) == cp && !((cp == '\t' || cp == '\r' || cp == '\b' || cp == ESC));
  if (iscntrl) {
    cp = cp + 96;
  }

  bool isshift= false;
  if (!iscntrl) {
    isshift = cp >= 'A' && cp <= 'Z';
    if (isshift) {
      k.key.alternate_codepoint = cp;
      cp = cp + 32;
    }

    if (!isshift) {
      isshift = (cp < LENGTH(VELVET_SHIFT_TABLE)) && VELVET_SHIFT_TABLE[cp];
      if (isshift) {
        k.key.alternate_codepoint = cp;
        cp = VELVET_SHIFT_TABLE[cp];
      }
    }
  }

  k.key.codepoint = cp;
  k.modifiers = ((iscntrl * MODIFIER_CTRL) | (isshift * MODIFIER_SHIFT));
  k.key.kitty_terminator = 'u';

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

static void velvet_input_send_vk(struct velvet *v, struct velvet_key_event e);

static bool skip_non_tiled(struct velvet_window *w, void *) {
  return w->layer != VELVET_LAYER_TILED || w->dragging;
}

static void dispatch_border_drag(struct velvet *v, struct mouse_sgr sgr, struct velvet_window_hit hit) {
  struct velvet_input *in = &v->input;
  struct velvet_window *drag = nullptr;
  if (in->dragging.id) {
    drag = velvet_scene_get_window_from_id(&v->scene, in->dragging.id);
    if (!drag) {
      in->dragging = (struct velvet_input_drag_event){0};
      return;
    }
  }

  if (!drag) drag = hit.win;
  assert(drag);
  if (sgr.event_type == mouse_click && sgr.trigger == mouse_down && (hit.where & VELVET_HIT_BORDER)) {
    in->dragging = (struct velvet_input_drag_event){
        .id = drag->id,
        .drag_start =
            {
                .x = sgr.column,
                .y = sgr.row,
            },
        .type = (hit.where & VELVET_HIT_TITLE) ? DRAG_MOVE : DRAG_RESIZE,
        .loc = hit.where,
    };
    drag->dragging = true;
    if (in->dragging.type == DRAG_MOVE && drag->layer == VELVET_LAYER_TILED) {
      struct rect new_size = drag->rect.window;
      new_size.h = v->scene.ws.h / 2;
      new_size.w = v->scene.ws.w / 2;
      velvet_window_resize(drag, new_size);
    }
    in->dragging.drag_start.win = drag->rect.window;
    drag->layer = VELVET_LAYER_FLOATING;
    velvet_scene_set_focus(&v->scene, drag);
  } else if (sgr.event_type == mouse_click && sgr.trigger == mouse_up) {
    /* drop */
    drag->dragging = false;
    if (!velvet_window_visible(&v->scene, drag)) drag->tags = v->scene.view;

    bool is_drag_move = in->dragging.type == DRAG_MOVE;
    in->dragging = (struct velvet_input_drag_event){0};
    if (is_drag_move && sgr.modifiers & modifier_alt) {
      struct velvet_window_hit drop_hit = {0};
      velvet_scene_hit(&v->scene, sgr.column - 1, sgr.row - 1, &drop_hit, skip_non_tiled, nullptr);
      struct velvet_window *drop_target = drop_hit.win;
      if (drop_target && drop_target != drag) {
        drag->layer = VELVET_LAYER_TILED;
        bool above /* otherwise below */ = (sgr.row - drop_target->rect.window.y) < (drop_target->rect.window.h / 2);
        int target_index = vec_index(&v->scene.windows, drag);
        int drop_index = vec_index(&v->scene.windows, drop_target);

        /* account for the fact that we temporarily remove the window */
        if (target_index < drop_index) drop_index--;
        if (!above) drop_index++;
        struct velvet_window t = *drag;
        vec_remove(&v->scene.windows, drag);
        vec_insert(&v->scene.windows, drop_index, &t);
        velvet_scene_set_focus(&v->scene, drop_target);
      } else {
        drag->layer = VELVET_LAYER_TILED;
      }
    }
  } else if (in->dragging.id && (sgr.event_type & mouse_move)) {
    struct rect w = in->dragging.drag_start.win;
    int delta_x = sgr.column - in->dragging.drag_start.x;
    int delta_y = sgr.row - in->dragging.drag_start.y;

    enum velvet_window_hit_location h = in->dragging.loc;
    if (h & VELVET_HIT_TITLE) {
      /* drag move */
      w.x += delta_x;
      w.y += delta_y;
    } else if (h & VELVET_HIT_BORDER) {
      /* drag resize */
      h &= ~VELVET_HIT_BORDER;
      if (h & VELVET_HIT_BORDER_TOP) {
        w.y += delta_y;
        w.h -= delta_y;
      }
      if (h & VELVET_HIT_BORDER_BOTTOM) {
        w.h += delta_y;
      }
      if (h & VELVET_HIT_BORDER_LEFT) {
        w.x += delta_x;
        w.w -= delta_x;
      }
      if (h & VELVET_HIT_BORDER_RIGHT) {
        w.w += delta_x;
      }
    }
    velvet_window_resize(drag, w);
  }
}

static void scroll_to_bottom(struct velvet *v) {
  struct velvet_window *focus = velvet_scene_get_focus(&v->scene);
  if (focus) screen_set_scroll_offset(&focus->emulator.primary, 0);
}

static void send_bracketed_paste(struct velvet *v) {
  struct velvet_input *in = &v->input;
  struct velvet_window *focus = velvet_scene_get_focus(&v->scene);
  if (focus) {
    bool enclose = focus->emulator.options.bracketed_paste;
    uint8_t *start = in->command_buffer.content;
    size_t len = in->command_buffer.len;
    if (!enclose) {
      start += bracketed_paste_start.len;
      len -= bracketed_paste_start.len + bracketed_paste_end.len;
    }
    struct u8_slice s = {.content = start, .len = len};
    string_push_slice(&focus->emulator.pending_input, s);
  }
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
      default:
        struct u8_slice raw = string_as_u8_slice(v->input.command_buffer);
        for (int i = 0; i < LENGTH(named_keys); i++) {
          struct velvet_key k = named_keys[i];
          if (k.escape && u8_slice_equals(raw, u8_slice_from_cstr(k.escape))) {
            struct velvet_key_event evt = {.key = k, .legacy = true};
            dispatch_key_event(v, evt);
            break;
          }
        }
        break;
      }
    }
    string_clear(&v->input.command_buffer);
    in->state = VELVET_INPUT_STATE_NORMAL;
  }
#undef KEY
#undef CSI
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
      /* simulate key press and release since a client using kitty keys may expect this. */
      struct velvet_key_event e = k->key;
      e.type = KEY_PRESS;
      root->on_key(root, e);
      e.type = KEY_RELEASE;
      root->on_key(root, e);
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

/* strip unsupported modifiers and collapse equivalent key states for easier comparisons */
static struct velvet_key_event key_cannonicalize(struct velvet_key_event e) {
  uint32_t c = e.key.codepoint;
  /* treat alt the same as meta for all practical purposes */
  if ((e.modifiers & MODIFIER_ALT) || (e.modifiers & MODIFIER_META)) {
    e.modifiers &= ~MODIFIER_ALT;
    e.modifiers |= MODIFIER_META;
  }
  e.modifiers &= ~(MODIFIER_HYPER | MODIFIER_NUM_LOCK | MODIFIER_CAPS_LOCK);
  if (e.key.escape && e.key.escape[0] && !e.key.escape[1]) {
    uint8_t ch = e.key.escape[0];
    if ((e.modifiers & MODIFIER_SHIFT) && ch >= 'a' && ch <= 'z') ch -= 32;
    if ((e.modifiers & MODIFIER_CTRL)) ch = ch & 0x1f;
    e.key.codepoint = ch;
  }
  if (e.key.codepoint && e.key.codepoint < 255) {
    e.key.kitty_terminator = 'u';
  }
  if (c) {
    /* if the key is a literal modifier key, strip the corresponding modifier */
    uint32_t unmask = 0;
    if (is_super(c)) unmask |= MODIFIER_SUPER;
    if (is_shift(c)) unmask |= MODIFIER_SHIFT;
    if (is_ctrl(c)) unmask |= MODIFIER_CTRL;
    if (is_meta(c)) unmask |= MODIFIER_ALT;
    if (is_alt(c)) unmask |= MODIFIER_ALT;
    if (is_hyper(c)) unmask |= MODIFIER_HYPER;
    if (is_caps_lock(c)) unmask |= MODIFIER_CAPS_LOCK;
    if (is_num_lock(c)) unmask |= MODIFIER_NUM_LOCK;
    e.modifiers &= ~unmask;
  }
  return e;
}

/* TODO: How should meta/alt behave */
static bool key_event_equals(struct velvet_key_event e1, struct velvet_key_event e2) {
  e1 = key_cannonicalize(e1);
  e2 = key_cannonicalize(e2);
  struct velvet_key k1 = e1.key, k2 = e2.key;
  if (e1.modifiers != e2.modifiers) 
    return false;
  if (k1.name || k2.name) 
    return k1.name && k2.name && strcasecmp(k1.name, k2.name) == 0;
  if ((k1.kitty_terminator || k2.kitty_terminator) && k1.kitty_terminator != k2.kitty_terminator) 
    return false;

  /* match alternating case if modifiers are equal. This allows us to match Shift+1 and Shift + ! for example */
  uint32_t a1 = k1.alternate_codepoint, a2 = k2.alternate_codepoint, c1 = k1.codepoint, c2 = k2.codepoint;
  if (a1 && (a1 == c2 || a1 == a2)) return true;
  if (a2 && (a2 == c1 || a2 == a1)) return true;
  if (c1 || c2) return c1 == c2;

  if (k1.escape || k2.escape) 
    return k1.escape && k2.escape && strcmp(k1.escape, k2.escape) == 0;

  /* both zero ?*/
  assert(!"trap");
}

static bool is_modifier(uint32_t codepoint);

// this is supposed to emulate VIM-like behavior
static void dispatch_key_event(struct velvet *v, struct velvet_key_event e) {
  assert(v);
  assert(v->input.keymap);
  struct velvet_keymap *k, *root, *current;
  if (!e.type) e.type = KEY_PRESS;
  current = v->input.keymap;
  root = current->root;
  assert(root);

  /* key release events are only supported in the root context (passing through to clients) */
  if (e.type == KEY_RELEASE) {
    if (current == root)
      root->on_key(root, e);
    return;
  }

  // cancel any scheduled unwind
  io_schedule_cancel(&v->event_loop, v->input.unwind_callback_token);

  // First check if this key matches a keybind in the current keymap
  for (k = current->first_child; k; k = k->next_sibling) {
    if (key_event_equals(k->key, e)) {
      if (k->first_child) {
        v->input.keymap = k;
        // If this sequence has both a mapping and a continuation,
        // defer the mapping until the intended sequence can be determined.
        v->input.unwind_callback_token =
            io_schedule(&v->event_loop, v->input.options.key_chain_timeout_ms, input_unwind_callback, v);
      } else {
        // this choord is terminal so on_key must be set
        assert(k->on_key);

        /* If the current keymap is still active because of a repeat,
         * only trigger a continuation if it allows repeating. */
        if (v->input.last_repeat && !k->is_repeatable) 
          break;

        uint64_t now = get_ms_since_startup();
        uint64_t last_repeat = v->input.last_repeat;
        if (!last_repeat) last_repeat = now;
        uint64_t timeout = v->input.options.key_repeat_timeout_ms;

        /* if `timeout` has elapsed since the previous repeat, break */
        if (k->is_repeatable && now - last_repeat > timeout) break;

        /* update the repeat timer */
        if (k->is_repeatable) v->input.last_repeat = now;

        k->on_key(k, e);

        /* reset the keymap unless the key is repeatable */
        if (!k->is_repeatable) v->input.keymap = k->root;
      }
      return;
    }
  }

  /* don't unwind or cancel chains if the processed key is a modifier */
  if (is_modifier(e.key.codepoint)) {
    root->on_key(root, e);
    return;
  }

  bool did_repeat = v->input.last_repeat > 0;
  v->input.last_repeat = 0;

  // ESC cancels any pending keybind
  if (e.key.codepoint == ESC && current != root) {
    v->input.keymap = root;
    return;
  }

  if (current == root) {
    root->on_key(root, e);
  } else if (did_repeat) {
    /* don't unwind the unresolved stack if we are handling a repeat timeout */
    v->input.keymap = root;
    dispatch_key_event(v, e);
  } else {
    // Since the current key was not matched, we should match the longest
    // prefix and then reinsert all pending keys
    velvet_input_unwind(v);
    dispatch_key_event(v, e);
  }
}

static void DISPATCH_KITTY_KEY(struct velvet *v, struct csi c) {
  uint32_t codepoint = c.params[0].primary;
  uint32_t alternate_codepoint = c.params[0].sub[0];
  uint32_t modifiers = c.params[1].primary;
  uint32_t event = c.params[1].sub[0];

  if (!codepoint) codepoint = 1;
  if (modifiers) modifiers -= 1;
  if (!event) event = KEY_PRESS;

  enum velvet_key_modifier *remap = v->input.options.modremap;
  enum velvet_key_modifier order[] = { MODIFIER_ALT, MODIFIER_CTRL, MODIFIER_SUPER };
  uint32_t unmask = ~(MODIFIER_ALT | MODIFIER_CTRL | MODIFIER_SUPER);
  uint32_t remapped_modifiers = modifiers & unmask;

  for (int i = 0; i < LENGTH(order); i++) {
    if (modifiers & order[i])  {
      if (remap[i]) remapped_modifiers |= remap[i];
      else remapped_modifiers |= order[i];
    }
  }

  modifiers = remapped_modifiers;


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
      struct velvet_key_event e = { .key = n, .legacy = true };
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
    k.modifiers |= MODIFIER_ALT;
    k.modifiers |= MODIFIER_META;
    k.legacy = true;
    dispatch_key_event(v, k);
  }
}

static void velvet_input_send_vk_basic(struct velvet *v, struct velvet_key vk, enum velvet_key_modifier m) {
  int n = 0;
  struct utf8 buf = {0};
  char *escape = nullptr;
  if (vk.kitty_terminator == 'u' && vk.codepoint && vk.codepoint < 255) {
    uint32_t send = vk.alternate_codepoint && vk.alternate_codepoint < 255 ? vk.alternate_codepoint : vk.codepoint;
    n = codepoint_to_utf8(send, &buf);
    escape = (char*)buf.utf8;
  } else if (vk.escape && vk.escape[0]) {
    escape = vk.escape;
    n = strlen(escape);
  }

  if (escape && escape[0]) {
    scroll_to_bottom(v);
    if (vk.codepoint == ESC) {
      send_byte(v, ESC);
    } else {
      bool is_meta = m & MODIFIER_ALT;
      bool is_cntrl = m & MODIFIER_CTRL;

      if (is_meta) send_byte(v, ESC);
      bool is_byte = !escape[1];
      if (is_byte && is_cntrl) {
        char ch = escape[0] & 0x1f;
        send_byte(v, ch);
      } else {
        for (int i = 0; i < n; i++) send_byte(v, escape[i]);
      }
    }
  }
}

static bool is_keypad(struct velvet_key k) {
  uint32_t codepoint = k.codepoint;
  uint32_t kp_0 = 57399; /* first kp codepoint */
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
velvet_input_send_kitty_encoding(struct velvet *v, struct velvet_key_event e, enum kitty_keyboard_options o) {
  struct velvet_window *f = velvet_scene_get_focus(&v->scene);
  assert(f);
  if (!e.type) e.type = KEY_PRESS;
  bool is_mod = is_modifier(e.key.codepoint);

  bool do_send = (o & KITTY_KEYBOARD_REPORT_EVENT_TYPES) || e.type < KEY_RELEASE;
  if (is_mod && !(o & KITTY_KEYBOARD_REPORT_ALL_KEYS_AS_ESCAPE_CODES)) do_send = false;

  if (do_send && e.type == KEY_RELEASE) {
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
      || (e.type == KEY_RELEASE) /* release events must be encoded. In my mind, repeat events should also
                                    be encoded, but this is not how other emulators do it. */
      || (e.key.escape && e.key.escape[0] == ESC) /* encode any key which is otherwise encoded with a leading ESC */
      || (is_keypad(e.key) && !is_keypad_with_text(e.key)) /* all non-text keypad keys */
      || (e.modifiers & (MODIFIER_CTRL | MODIFIER_ALT));   /* disambiguate ctrl / alt modifiers */

  if (!do_send_encoded) {
    /* send the base symbol. This applies to most pure-text keys */
    if (!is_mod) velvet_input_send_vk_basic(v, e.key, e.modifiers);
    return;
  }

  uint32_t modifiers = e.modifiers + 1; /* modifiers are encoded as +1 */
  bool do_encode_event = (o & KITTY_KEYBOARD_REPORT_EVENT_TYPES) && e.type > KEY_PRESS;
  bool do_encode_modifier = do_encode_event || modifiers > 1;

  enum kitty_keyboard_options flags = KITTY_KEYBOARD_REPORT_ASSOCIATED_TEXT | KITTY_KEYBOARD_REPORT_ALL_KEYS_AS_ESCAPE_CODES;

  /* associated text is not included on key release events */
  bool do_encode_associated_text = e.type != KEY_RELEASE && (o & flags) == flags && e.associated_text.n;

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

static void velvet_input_send_vk(struct velvet *v, struct velvet_key_event e) {
  struct velvet_window *f = velvet_scene_get_focus(&v->scene);
  if (!f) return;

  enum kitty_keyboard_options o = f->emulator.options.kitty[f->emulator.options.alternate_screen].options;
  if (o == KITTY_KEYBOARD_NONE || e.legacy) {
    if (e.type != KEY_RELEASE) {
      if (!is_modifier(e.key.codepoint)) {
        velvet_input_send_vk_basic(v, e.key, e.modifiers);
      }
    }
  } else {
    velvet_input_send_kitty_encoding(v, e, o);
  }
}

void velvet_input_send(struct velvet_keymap *k, struct velvet_key_event e) {
  if (e.removed) return;
  velvet_input_send_vk(k->data, e);
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
    find_key("ESCAPE", &k.key);
    dispatch_key_event(v, k);
  }
}

static void dispatch_focus(struct velvet *v, struct csi c) {
  struct velvet_window *focus = velvet_scene_get_focus(&v->scene);
  if (focus && focus->emulator.options.focus_reporting) {
    send(v, c.final == 'O' ? vt_focus_out : vt_focus_in);
  }
  if (c.final == 'I' && v->input.input_socket) v->focused_socket = v->input.input_socket;
}

void DISPATCH_FOCUS_OUT(struct velvet *v, struct csi c) {
  dispatch_focus(v, c);
}
void DISPATCH_FOCUS_IN(struct velvet *v, struct csi c) {
  dispatch_focus(v, c);
}
void DISPATCH_SGR_MOUSE(struct velvet *v, struct csi c) {
  struct velvet_input *in = &v->input;
  struct mouse_sgr sgr = mouse_sgr_from_csi(c);
  struct velvet_window_hit hit = {0};
  velvet_scene_hit(&v->scene, sgr.column - 1, sgr.row - 1, &hit, nullptr, nullptr);
  struct velvet_window *target = hit.win;

  if (!v->input.dragging.id) {
    if ((v->input.options.focus_follows_mouse || sgr.event_type == mouse_click) && hit.win) {
      velvet_scene_set_focus(&v->scene, hit.win);
    }
  }

  if (in->dragging.id || (hit.where & VELVET_HIT_BORDER)) {
    dispatch_border_drag(v, sgr, hit);
    return;
  }

  if (!target) return;

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
    return;
  }

  if (sgr.event_type != mouse_scroll) return;

  if (target->emulator.options.alternate_screen) {
    struct velvet_key k = {0};
    /* In the absence of mouse tracking, scroll up/down is treated as arrow keys in the alternate screen */
    if (sgr.scroll_direction == scroll_up) {
      /* NOTE: When kitty keyhandling is enabled, kitty encodes scrolling
       * as successive key press and key release events using the kitty protocol encoding,
       * whereas ghostty and alacritty uses the same encoding regardless of kitty protocol settings.
       * I don't think it matters much so we just do what's easiest.
       */
      find_key("SS3_UP", &k);
      struct velvet_key_event e = {.key = k, .type = KEY_PRESS};
      velvet_input_send_vk(v, e);
    } else if (sgr.scroll_direction == scroll_down) {
      find_key("SS3_DOWN", &k);
      struct velvet_key_event e = {.key = k, .type = KEY_PRESS};
      velvet_input_send_vk(v, e);
    }
  } else {
    struct screen *screen = vte_get_current_screen(&target->emulator);
    /* in the primary screen, scrolling affects the current view */
    int current_offset = screen_get_scroll_offset(screen);
    if (sgr.scroll_direction == scroll_up) {
      int num_lines = screen_get_scroll_height(screen);
      screen_set_scroll_offset(screen, MIN(num_lines, current_offset + in->options.scroll_multiplier));
    } else if (sgr.scroll_direction == scroll_down) {
      screen_set_scroll_offset(screen, MAX(0, current_offset - in->options.scroll_multiplier));
    }
  }
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

  struct u8_slice_codepoint_iterator it = { .src = s };
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

void velvet_keymap_unmap(struct velvet_keymap *root, struct u8_slice key_sequence) {
  struct velvet_key_iterator it = { .src = key_sequence };
  struct velvet_keymap *to_remove = root;
  for (; to_remove && velvet_key_iterator_next(&it);) {
    for (to_remove = to_remove->first_child; to_remove && !key_event_equals(to_remove->key, it.current);
         to_remove = to_remove->next_sibling);
  }
  if (to_remove) velvet_keymap_remove_internal(to_remove);
}

struct velvet_keymap *velvet_keymap_map(struct velvet_keymap *root, struct u8_slice key_sequence) {
  struct velvet_keymap *prev, *chain, *parent;
  assert(root);
  assert(key_sequence.len);

  struct velvet_key_iterator it = { .src = key_sequence };
  struct velvet_key_iterator test = it;
  for (; velvet_key_iterator_next(&test););
  
  if (test.invalid) {
    velvet_log("Rejecting keymap %.*s: The key %.*s was not recognized.",
               (int)key_sequence.len,
               key_sequence.content,
               (int)test.current_range.len,
               test.current_range.content);
    return nullptr;
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
  for (; velvet_key_iterator_next(&it); ) {
    velvet_input_send_vk(in, it.current);
  }
}
