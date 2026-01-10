#ifndef VELVET_H
#define VELVET_H

#include "collections.h"
#include "io.h"
#include "lua.h"
#include "platform.h"
#include "velvet_scene.h"
#include "velvet_keyboard.h"

enum velvet_input_state {
  VELVET_INPUT_STATE_NORMAL,
  VELVET_INPUT_STATE_ESC,
  VELVET_INPUT_STATE_APPLICATION_KEYS,
  VELVET_INPUT_STATE_CSI,
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
  bool is_repeatable;
  struct velvet_key_event key;
};

struct velvet_input_options {
  bool focus_follows_mouse;
  /* When one mapping is a prefix of another, we resolve the shorter mapping after this delay. */
  uint64_t key_chain_timeout_ms;
  /* When a keybind repeatable mapping is triggered, allow retriggers within this window */
  uint64_t key_repeat_timeout_ms;
  /* how many lines are scrolled at a time */
  int scroll_multiplier;
  /* remap modifiers */
  enum velvet_key_modifier modremap[3];
};

struct velvet_keymap_deferred_action {
  struct velvet_keymap *keymap;
  struct velvet_key_event key;
};

enum velvet_input_drag_event_type {
  DRAG_MOVE,
  DRAG_RESIZE,
};

struct velvet_input_drag_event {
  struct {
    /* the initial position of the mosue */
    int x, y;
    /* the initial position of the dragged client */
    struct rect win;
  } drag_start;
  /* the id of the client being dragged */
  uint64_t id;
  enum velvet_window_hit_location loc;
  enum velvet_input_drag_event_type type;
};

struct velvet_input {
  enum velvet_input_state state;
  struct string command_buffer;
  struct velvet_input_options options;
  struct velvet_keymap *keymap;
  uint64_t last_repeat;
  struct velvet_input_drag_event dragging;
  int input_socket;
  io_schedule_id unwind_callback_token;
};

struct velvet_session_features {
  bool no_repeat_wide_chars;
};

struct velvet_session {
  int socket;                   // socket connection
  struct string pending_output; // buffered output
  int input;                    // stdin
  int output;                   // stdout
  struct rect ws;
  struct string cwd;
  struct {
    struct string buffer; // partial commands
    int lines;
  } commands;
  struct velvet_session_features features;
};

struct velvet {
  lua_State *L;
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
  io_schedule_id active_render_token;
  io_schedule_id idle_render_token;
  /* velvet will try to render when io is idle, but if io is constantly busy
   * it will try to render at least in this interval */
  int min_ms_per_frame;
};

void velvet_input_send(struct velvet_keymap *k, struct velvet_key_event e);
void velvet_loop(struct velvet *velvet);
void velvet_destroy(struct velvet *velvet);
/* Process keys in the root keymap. This can be used in e.g. a mapping to map asd->def.
 * This input will not be parsed for CSI sequences or any current keymap. */
void velvet_input_put(struct velvet *in, struct u8_slice str);
/* Process e.g. standard input from the keyboard. This input will be parsed for CSI sequences and matched against the
 * current keymap. */
void velvet_input_process(struct velvet *in, struct u8_slice str);
void velvet_input_destroy(struct velvet_input *v);
void velvet_keymap_unmap(struct velvet_keymap *root, struct u8_slice key_sequence);
struct velvet_keymap * velvet_keymap_map(struct velvet_keymap *root, struct u8_slice keys);
void velvet_input_unwind(struct velvet *v);
struct velvet_session *velvet_get_focused_session(struct velvet *v);
void velvet_detach_session(struct velvet *velvet, struct velvet_session *s);
void velvet_session_destroy(struct velvet *velvet, struct velvet_session *s);
void velvet_ensure_render_scheduled(struct velvet *velvet);

[[maybe_unused]] static struct velvet_input velvet_input_default = {
    .options =
        {
            .focus_follows_mouse = true,
            .key_chain_timeout_ms = 2000,
            .key_repeat_timeout_ms = 500,
            .scroll_multiplier = 3,
        },
};

#endif
