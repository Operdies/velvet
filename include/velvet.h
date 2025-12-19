#ifndef VELVET_H
#define VELVET_H

#include "collections.h"
#include "io.h"
#include "platform.h"
#include "velvet_scene.h"
#include "velvet_keyboard.h"

enum velvet_input_state {
  VELVET_INPUT_STATE_NORMAL,
  VELVET_INPUT_STATE_ESC,
  VELVET_INPUT_STATE_APPLICATION_KEYS,
  VELVET_INPUT_STATE_CSI,
};

/* masks matching the Kitty keyboard protocol spec: https://sw.kovidgoyal.net/kitty/keyboard-protocol/ */
enum velvey_key_modifier {
  MODIFIER_SHIFT     = 0b1,        //(1)
  MODIFIER_ALT       = 0b10,       //(2)
  MODIFIER_CTRL      = 0b100,      //(4)
  MODIFIER_SUPER     = 0b1000,     //(8)
  MODIFIER_HYPER     = 0b10000,    //(16)
  MODIFIER_META      = 0b100000,   //(32)
  MODIFIER_CAPS_LOCK = 0b1000000,  //(64)
  MODIFIER_NUM_LOCK  = 0b10000000, //(128)
};


struct velvet_key_event {
  struct velvet_key key;
  enum velvey_key_modifier modifiers;
  /* set if the mapping was removed. If removed is set, no other fields will be set. */
  bool removed;
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
  bool may_repeat;
  struct velvet_key_event key;
};

struct velvet_input_options {
  bool focus_follows_mouse;
  /* When one mapping is a prefix of another, we resolve the shorter mapping after this delay. */
  int key_chain_timeout_ms;
  /* When a keybind repeatable mapping is triggered, allow retriggers within this window */
  int key_repeat_timeout_ms;
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
  struct {
    struct string buffer; // partial commands
    int lines;
  } commands;
};

struct velvet {
  struct velvet_scene scene;
  struct velvet_input input;
  struct io event_loop;
  struct vec /* struct session */ sessions;
  /* this is modified by events such as receiving focus IN/OUT events, new sessions attaching, etc */
  int focused_socket;
  int socket;
  int signal_read;
  bool quit;
  bool daemon;
};

void velvet_input_send(struct velvet_keymap *k, struct velvet_key_event e);
void velvet_loop(struct velvet *velvet);
void velvet_destroy(struct velvet *velvet);
void velvet_process_input(struct velvet *in, struct u8_slice str);
void velvet_input_destroy(struct velvet_input *v);
void velvet_keymap_unmap(struct velvet_keymap *root, struct u8_slice key_sequence);
struct velvet_keymap * velvet_keymap_map(struct velvet_keymap *root, struct u8_slice keys);
void velvet_input_unwind(struct velvet *v);
struct velvet_session *velvet_get_focused_session(struct velvet *v);
void velvet_detach_session(struct velvet *velvet, struct velvet_session *s);

static struct velvet_input velvet_input_default = {
    .options =
        {
            .focus_follows_mouse = true,
            .key_chain_timeout_ms = 1000,
            .key_repeat_timeout_ms = 1000,
        },
};

#endif
