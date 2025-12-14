#ifndef VELVET_H
#define VELVET_H

#include "collections.h"
#include "io.h"
#include "platform.h"
#include "velvet_scene.h"

enum velvet_input_state {
  VELVET_INPUT_STATE_NORMAL,
  VELVET_INPUT_STATE_ESC,
  VELVET_INPUT_STATE_CSI,
};

enum mouse_modifiers { modifier_none = 0, modifier_shift = 4, modifier_alt = 8, modifier_ctrl = 16 };

enum kitty_modifiers {
  KITTY_MODIFIER_SHIFT     = 0b1,        //(1)
  KITTY_MODIFIER_ALT       = 0b10,       //(2)
  KITTY_MODIFIER_CTRL      = 0b100,      //(4)
  KITTY_MODIFIER_SUPER     = 0b1000,     //(8)
  KITTY_MODIFIER_HYPER     = 0b10000,    //(16)
  KITTY_MODIFIER_META      = 0b100000,   //(32)
  KITTY_MODIFIER_CAPS_LOCK = 0b1000000,  //(64)
  KITTY_MODIFIER_NUM_LOCK  = 0b10000000, //(128)
};

struct velvet_key_event {
  struct utf8 symbol;
  enum kitty_modifiers modifiers;
};

struct velvet_keymap;
typedef void (on_key)(struct velvet_keymap *k, struct velvet_key_event e);

struct velvet_keymap {
  /* the root keymap in this node */
  struct velvet_keymap *root;
  /* the parent keymap in this node */
  struct velvet_keymap *parent;
  /* the next sibling keymap */
  struct velvet_keymap *next_sibling;
  /* the new keymap after this keymap is activated. If null, this keymap remains active. */
  struct velvet_keymap *first_child;
  /* callback called when this keymap is activated, and on every subsequent keystroke.
   * return true to reset the keymap to the root keymap.
   */
  on_key *on_key;
  void *data;
  struct velvet_key_event key;
};

struct velvet_key_sequence {
  struct velvet_key_event *sequence;
  int len;
};

struct velvet_input_options {
  bool focus_follows_mouse;
  int keybind_timeout_ms;
};

struct velvet_keymap_deferred_action {
  struct velvet_keymap *keymap;
  struct velvet_key_event key;
};

struct velvet_input {
  enum velvet_input_state state;
  struct string command_buffer;
  struct velvet_input_options options;
  struct velvet_keymap *keymap;
};

struct velvet_session {
  int socket;                   // socket connection
  struct string pending_output; // buffered output
  int input;                    // stdin
  int output;                   // stdout
  struct platform_winsize ws;
};

struct velvet {
  struct velvet_scene scene;
  struct velvet_input input;
  struct io event_loop;
  struct vec /* struct session */ sessions;
  /* this is modified by events such as receiving focus IN/OUT events, new sessions attaching, etc */
  size_t active_session;
  int socket;
  int signal_read;
  bool quit;
};

void velvet_input_send(struct velvet_keymap *k, struct velvet_key_event e);
void velvet_loop(struct velvet *velvet);
void velvet_destroy(struct velvet *velvet);
void velvet_process_input(struct velvet *in, struct u8_slice str);
void velvet_input_destroy(struct velvet_input *v);
struct velvet_keymap *velvet_keymap_add(struct velvet_keymap *root, struct velvet_key_sequence keys, on_key *callback, void *data);
void velvet_input_unwind(struct velvet *v);

static bool key_event_equals(struct velvet_key_event k1, struct velvet_key_event k2) {
  return k1.modifiers == k2.modifiers && k1.symbol.numeric == k2.symbol.numeric;
}

static struct velvet_input velvet_input_default = {
    .options =
        {
            .focus_follows_mouse = true,
            .keybind_timeout_ms = 1000,
        },
};

#endif
