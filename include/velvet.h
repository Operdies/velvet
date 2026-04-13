#ifndef VELVET_H
#define VELVET_H

#include "collections.h"
#include "io.h"
#include "lua.h"
#include "velvet_scene.h"

enum velvet_coroutine_exit_code {
  /* set when the chunk succesfully runs to completion */
  VELVET_COROUTINE_SUCCESS = 0,
  /* set when the lua chunk fails to compile */
  VELVET_COROUTINE_SYNTAX_ERROR = 1,
  /* set when the provided chunk throws an error */
  VELVET_COROUTINE_ERROR = 2,
  /* set when the server kills the chunk */
  VELVET_COROUTINE_KILLED_RELOAD = 3,
  /* set when the server exits */
  VELVET_COROUTINE_SERVER_EXITED = 4,
  /* An internal error has occurred if this happens. */
  VELVET_COROUTINE_UNEXPECTED_WRITE = 5,
};

enum velvet_input_state {
  VELVET_INPUT_STATE_NORMAL,
  VELVET_INPUT_STATE_ESC,
  VELVET_INPUT_STATE_APPLICATION_KEYS,
  VELVET_INPUT_STATE_CSI,
};

/* used for sendning a lua chunk to the server via shared memory */
#define VV_LUA_MAGIC 0xCAFEBEEF
struct vv_lua_payload {
  size_t magic;
  size_t chunk_offset;
  size_t chunk_length;
};

struct velvet_input_options {
  /* The number of lines scrolled per scroll wheel tick */
  int scroll_multiplier;
};

struct velvet_input {
  struct velvet_api_coordinate last_mouse_position;
  enum velvet_input_state state;
  struct string command_buffer;
  struct velvet_input_options options;
  int input_socket;
};

struct velvet_session_features {
  bool no_repeat_multibyte_graphemes; // compatibility with some terminals with poor multibyte handling
};

struct velvet_coroutine {
  int socket; /* Socket connection. Used for the status code on close */
  int out_fd, err_fd; /* stdout / stderr for the coroutine */
  struct string pending_output;
  struct string pending_error;
  lua_State *coroutine;
  enum velvet_coroutine_exit_code status;
};

struct velvet_session {
  int socket;                   // socket connection
  struct string pending_output; // buffered output
  int input;                    // stdin
  int output;                   // stdout
  struct rect ws;               // window size
  struct string command_buffer; // vv lua commands
  struct velvet_session_features features;
};

struct velvet_kvp {
  struct string key;
  struct string value;
};

struct velvet {
  /* main lua context */
  lua_State *L;
  /* executing lua context. set when using coroutines */
  lua_State *current;
  struct velvet_scene scene;
  struct velvet_input input;
  struct io event_loop;
  struct vec /* struct velvet_session */ sessions;
  struct vec /* struct velvet_coroutine */ coroutines;
  /* this is modified by events such as receiving focus IN/OUT events, new sessions attaching, etc */
  int focused_socket;
  int socket_cmd_sender;
  int socket;
  int signal_read;
  bool quit;
  bool daemon;
  io_schedule_id active_render_token;
  io_schedule_id idle_render_token;
  /* velvet will try to render when io is idle, but if io is constantly busy
   * it will try to render at least in this interval */
  int min_ms_per_frame;
  const char *startup_directory;
  /* if render_invalidated is set, velvet will schedule a render at an appropriate time. */
  bool _render_invalidated;
  const char *render_invalidate_reason;
  struct vec /* velvet_kvp */ stored_strings;
  /* defined at init time in velvet_lua.c */
  lua_Integer coroutine_wrapper_function;
};

void velvet_force_full_redraw(struct velvet *scene);
void velvet_invalidate_render(struct velvet *velvet, [[maybe_unused]] const char *reason);
void velvet_loop(struct velvet *velvet);
void velvet_destroy(struct velvet *velvet);
/* Process keys in the root keymap. This can be used in e.g. a mapping to map asd->def.
 * This input will not be parsed for CSI sequences or any current keymap. */
void velvet_input_send_key_to_window(struct velvet *v, struct velvet_api_window_key_event key_event, struct velvet_window *win);
void velvet_input_send_keys(struct velvet *v, struct u8_slice str, int win_id);
void velvet_input_paste_text(struct velvet *v, struct u8_slice str, int win_id);
void velvet_input_send_mouse_move(struct velvet *v, struct velvet_api_mouse_move_event_args move);
void velvet_input_send_mouse_click(struct velvet *v, struct velvet_api_mouse_click_event_args click);
void velvet_input_send_mouse_scroll(struct velvet *v, struct velvet_api_mouse_scroll_event_args scroll);
/* Process e.g. standard input from the keyboard. This input will be parsed for CSI sequences and matched against the
 * current keymap. */
void velvet_input_process(struct velvet *in, struct u8_slice str);
void velvet_input_destroy(struct velvet_input *v);
void velvet_coroutine_destroy(struct velvet *velvet, struct velvet_coroutine *s);
struct velvet_session *velvet_get_focused_session(struct velvet *v);
void velvet_set_focused_session(struct velvet *v, int socket_fd);
void velvet_detach_session(struct velvet *velvet, struct velvet_session *s);
void velvet_session_destroy(struct velvet *velvet, struct velvet_session *s);
bool window_visible(struct velvet *v, struct velvet_window *w);
void velvet_lua_execute_chunk(struct velvet *v, struct u8_slice chunk, int source_socket);

[[maybe_unused]] static struct velvet_input velvet_input_default = {0};

#endif
