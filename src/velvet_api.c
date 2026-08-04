#include "velvet_api.h"
#include "lauxlib.h"
#include "platform.h"
#include "utf8proc/utf8proc.h"
#include "velvet.h"
#include "velvet_lua.h"
#include "velvet_process.h"
#include <ctype.h>
#include <dirent.h>
#include <lua.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

static struct string stringbuf = {0};
static struct vec envlist = vec(char*);

_Noreturn static void lua_bail(lua_State *L, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  lua_pushvfstring(L, fmt, ap);
  va_end(ap);
  lua_error(L);
  /* lua_error longjumps back to lua call site */
  assert(!"Unreachable");
}

#define bail(...) lua_bail(L, __VA_ARGS__)

static int lua_debug_traceback_handler(lua_State *L) {
  luaL_traceback(L, L, lua_tostring(L, 1), 1);
  return 1;
}

static struct velvet_window *check_lua_window(struct velvet *v, int win) {
  struct lua_State *L = v->current;
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win);
  if (!w) bail("Window id %I is not valid.", win);
  if (!w->is_lua_window) bail("Window id %I is not a lua window.", win);
  assert(w);
  return w;
}

static struct velvet_process *check_process(struct velvet *v, int proc) {
  struct lua_State *L = v->current;
  struct velvet_process *p;
  vec_find(p, v->processes, p->id == proc);
  if (!p) bail("Process id %I is not valid.", proc);
  return p;
}

static struct velvet_window *check_window(struct velvet *v, int win) {
  struct lua_State *L = v->current;
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win);
  if (!w) bail("Window id %I is not valid.", win);
  assert(w);
  return w;
}

static struct velvet_window *check_process_window(struct velvet *v, int win) {
  struct lua_State *L = v->current;
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win);
  if (!w) bail("Window id %I is not valid.", win);
  if (w->is_lua_window) bail("Window id %I is a lua window.", win);
  assert(w);
  return w;
}

struct velvet_wordsplit_iterator {
  struct u8_slice src;
  size_t cursor;
  struct string current;
  bool reject;
  char *reject_reason;
};

static int escape_char(char ch) {
  switch (ch) {
  case 'a': return '\a';   /* alert/bell */
  case 'b': return '\b';   /* backspace */
  case 'e': return '\033'; /* escape */
  case 'f': return '\f';   /* form feed */
  case 'n': return '\n';   /* newline */
  case 'r': return '\r';   /* carriage return */
  case 't': return '\t';   /* tab */
  case 'v': return '\v';   /* vertical tab */
  case '\\': return '\\';
  case '\'': return '\'';
  case ' ': return ' ';
  case '$': return '$';
  case '"': return '"';
  case '0': return '\0'; /* null */
  default: return -1;    /* unknown escape */
  }
}

static bool wordsplit_iterator_next(struct velvet_wordsplit_iterator *it) {
  struct string *s = &it->current;
  string_clear(s);
  for (; it->cursor < it->src.len && isspace(it->src.content[it->cursor]); it->cursor++);
  if (it->cursor >= it->src.len) return false;

  size_t cursor = it->cursor;

  char quote = 0;
  bool inhibit = false;
  bool inhibit_inhibit = false;

  for (; cursor < it->src.len; cursor++) {
    char ch = it->src.content[cursor];
    char peek = (cursor + 1) < it->src.len ? it->src.content[cursor + 1] : 0;
    if (inhibit) {
      /* unset prev if it was escaped. e.g. the word \$'xyz' should expand to $xyz, and
       * ansi parsing should not be enabled inside the quotes. */
      inhibit = false;
      int esc = escape_char(ch);
      /* invalid escape command */
      if (esc == -1) {
        it->reject = true;
        it->reject_reason = "Invalid escape character.";
        return false;
      }
      string_push_char(s, esc);
    } else if (!quote && ch == '$' && peek == '\'') {
      quote = '\'';
      cursor++;
    } else if (ch == '\\' && !inhibit_inhibit) {
      inhibit = true;
    } else if (quote && ch != quote) {
      string_push_char(s, ch);
    } else if (quote && ch == quote) {
      quote = 0;
      inhibit_inhibit = false;
    } else if (ch == '\'' || ch == '"') {
      quote = ch;
      inhibit_inhibit = ch == '\'';
    } else if (isspace(ch)) {
      /* adjacent quotes and characters are joined as a single argument,
       * so we should only break on end of string, or on whitespace. */
      break;
    } else {
      string_push_char(s, ch);
    }
  }

  if (inhibit) {
    it->reject = true;
    it->reject_reason = "trailing escape";
  }

  if (quote) {
    it->reject = true;
    it->reject_reason = "unterminated quote";
  }

  it->cursor = cursor;
  return !it->reject;
}

static lua_stackRetCount vv_api_get_processes(struct velvet *v) {
  lua_State *L = v->current;
  lua_newtable(L);
  lua_Integer index = 1;
  struct velvet_process *p;
  /* hide non-regular windows from the LUA api for now */
  vec_foreach(p, v->processes) {
    lua_pushinteger(L, p->id);
    lua_seti(L, -2, index++);
  }
  return 1;
}

static char *signal_name(int signal) {
  /* the exact signal names used in spec.lua */
  char *names[] = {
      [SIGHUP] = "hup",
      [SIGINT] = "int",
      [SIGQUIT] = "quit",
      [SIGKILL] = "kill",
      [SIGUSR1] = "usr1",
      [SIGUSR2] = "usr1",
      [SIGALRM] = "alrm",
      [SIGTERM] = "term",
      [SIGSTOP] = "stop",
      [SIGCONT] = "cont",
  };

  if (LENGTH(names) <= signal) return NULL;
  return names[signal];
}

static void vv_api_process_kill(struct velvet *v, lua_Integer id, enum velvet_api_unix_signal signal) {
  /* defined in the same order as the signal enum from spec.lua */
  int signal_lookup[] = {
      SIGHUP,
      SIGINT,
      SIGQUIT,
      SIGKILL,
      SIGUSR1,
      SIGUSR2,
      SIGALRM,
      SIGTERM,
      SIGSTOP,
      SIGCONT,
  };

  assert(LENGTH(signal_lookup) > signal);
  struct velvet_process *p = check_process(v, id);
  velvet_process_kill(v, p, signal_lookup[signal]);
}

static void split_and_push_string_array(lua_State *L) {
  struct velvet_wordsplit_iterator it = {0};
  it.src.content = (uint8_t *)luaL_checklstring(L, -1, &it.src.len);
  lua_newtable(L);

  int index = 1;
  while (wordsplit_iterator_next(&it)) {
    lua_pushlstring(L, (char *)it.current.content, it.current.len);
    lua_seti(L, -2, index++);
  }
  string_destroy(&it.current);
  if (it.reject) {
    bail("Invalid command: %s", it.reject_reason); /* TODO: Explain the problem */
  }
}

/* fetch func_ref from the lua registry and invoke it with `nargs` parameters.
 * nargs will be popped. */
static void pcall_func_with_args(lua_State *L, int nargs) {
  int argtop = lua_gettop(L) - nargs + 1;

  /* insert msgh right before the function and args */
  int msgh = argtop - 1;
  lua_pushcfunction(L, lua_debug_traceback_handler);
  lua_insert(L, msgh);

  if (lua_pcall(L, nargs, 0, msgh) != LUA_OK) {
    struct velvet *v = *(struct velvet **)lua_getextraspace(L);
    struct u8_slice err = luaL_checkslice(L, -1);
    struct velvet_api_system_message_event_args event_args = {
        .level = VELVET_API_SEVERITY_ERROR,
        .message = err,
    };
    velvet_api_raise_system_message(v, event_args);
  }
  /* pop everything above and icluding msgh */
  lua_settop(L, msgh - 1);
}

void velvet_process_on_exit(struct velvet *v, struct velvet_process *p) {
  /* v->L is unset during reload */
  if (!v->L) return;
  struct u8_slice close = {0};
  velvet_process_on_stdout(v, p, close);
  velvet_process_on_stderr(v, p, close);

  if (p->callbacks.on_exit == LUA_NOREF) return;

  lua_State *L = v->L;
  lua_rawgeti(L, LUA_REGISTRYINDEX, p->callbacks.on_exit);
  luaL_unref(v->L, LUA_REGISTRYINDEX, p->callbacks.on_exit);
  lua_pushinteger(L, p->id);

  if (p->term_signal) {
    /* if terminated by signal, call on_exit(id, nil, signal); */
    lua_pushnil(L);
    char *signame = signal_name(p->term_signal);
    if (signame) {
      lua_pushstring(L, signame);
    } else {
      lua_pushfstring(L, "%d", p->term_signal);
    }
  } else {
    /* if terminated normally, call on_exit(id, exit_code, nil); */
    lua_pushinteger(L, p->exit_code);
    lua_pushnil(L);
  }
  pcall_func_with_args(L, 3);

  p->callbacks.on_exit = LUA_NOREF;
}

static void lua_push_slice_or_nil(lua_State *L, struct u8_slice data) {
  if (data.len == 0) lua_pushnil(L);
  else lua_pushlstring(L, (char*)data.content, data.len);
}

void velvet_process_on_stdout(struct velvet *v, struct velvet_process *p, struct u8_slice data) {
  if (!v->L) return;
  if (p->callbacks.on_stdout == LUA_NOREF) return;
  lua_rawgeti(v->L, LUA_REGISTRYINDEX, p->callbacks.on_stdout);
  if (data.len == 0) {
    /* stdout closed */
    luaL_unref(v->L, LUA_REGISTRYINDEX, p->callbacks.on_stdout);
    p->callbacks.on_stdout = LUA_NOREF;
  }
  lua_pushinteger(v->L, p->id);
  lua_push_slice_or_nil(v->L, data);
  lua_pushstring(v->L, "stdout");
  pcall_func_with_args(v->L, 3);
}

void velvet_process_on_stderr(struct velvet *v, struct velvet_process *p, struct u8_slice data) {
  if (!v->L) return;
  if (p->callbacks.on_stderr == LUA_NOREF) return;
  lua_rawgeti(v->L, LUA_REGISTRYINDEX, p->callbacks.on_stderr);
  if (data.len == 0) {
    /* stderr closed */
    luaL_unref(v->L, LUA_REGISTRYINDEX, p->callbacks.on_stderr);
    p->callbacks.on_stderr = LUA_NOREF;
  }
  lua_pushinteger(v->L, p->id);
  lua_push_slice_or_nil(v->L, data);
  lua_pushstring(v->L, "stderr");
  pcall_func_with_args(v->L, 3);
}

static lua_stackRetCount
vv_api_process_spawn(struct velvet *v, lua_stackIndex cmd, struct velvet_api_process_spawn_options options) {
  vec_clear(&envlist);

  lua_State *L = v->current;

  if (options.environment.set) {
    /* pin temporary env strings to ensure they are not gc'ed before fork() */
    lua_newtable(L);
    int env_pin = lua_gettop(L);
    int n_env = 0;

    lua_pushvalue(L, options.environment.value);
    luaL_checktable(L, -1);
    struct u8_slice key;

    /* perfrom validation and writing in two passes.
     * This simplifies the cleanup path. */

    /* first pass: verify the table only contains string->string values */
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      if (!lua_isstring(L, -2)) 
        bail("environment: expected string keys, got %s", lua_typename(L, lua_type(L, -2)));
      key = luaL_checkslice(L, -2);
      if (!lua_isstring(L, -1)) 
        bail("environment['%s']: expected string, got %s", key.content, lua_typename(L, lua_type(L, -1)));

      lua_pushvalue(L, -2); /* table, key, value, key */
      lua_insert(L, -2); /* table, key, key, value */
      lua_pushstring(L, "="); /* table, key, key, value, '=' */
      lua_insert(L, -2); /* table, key, key, '=', value */
      lua_concat(L, 3); /* table, key, <key=value> */
      const char *entry = lua_tostring(L, -1); /* entry = <key>=<value> */
      vec_push(&envlist, &entry);
      /* ensure entry is pinned until this function returns. The concat result is very unlikely to be garbage collected but let's just be safe*/
      lua_seti(L, env_pin, ++n_env); /* table, key */
    }

    /* NULL sentinel for env list */
    vec_push(&envlist, NULL);
  }

  lua_pushvalue(L, cmd); /* push cmd to top of stack */
  if (lua_isstring(L, -1)) {
    if (luaL_len(L, -1) == 0) bail("bad argument #1 to 'process_spawn' (string must not be empty)");
    split_and_push_string_array(L);
  }

  if (!lua_istable(L, -1)) bail("bad argument #1 to 'process_spawn'. string or string[] expected.");

  lua_Integer len = luaL_len(L, -1);
  if (len == 0) bail("bad argument #1 to 'process_spawn' (table must not be empty)");

  size_t argstart = envlist.length;
  for (int i = 1; i <= len; i++) {
    lua_geti(L, -1, i);
    if (!lua_isstring(L, -1)) {
      bail("bad argument #1 to 'process_spawn' (table must only contain strings)");
    }
    const char *arg = luaL_checkstring(L, -1);
    vec_push(&envlist, &arg);
    lua_pop(L, 1);
  }

  /* NULL sentinel for arglist */
  vec_push(&envlist, NULL);

  char **arglist = vec_nth(envlist, argstart);
  char **envp = options.environment.set ? vec_nth(envlist, 0) : NULL;
  char *wd = options.working_directory.set ? (char *)options.working_directory.value.content : NULL;
  struct velvet_process_stream_options streams = {
      .out = options.on_stdout.set, .err = options.on_stderr.set,
      .in = options.stdin_pipe,
  };
  lua_Integer proc_id = velvet_process_spawn(v, wd, arglist, envp, streams);
  if (proc_id < 0) {
    bail("Error starting %s: %s", arglist[0], strerror(-proc_id));
  }

  /* why do a scan when we know the index */
  struct velvet_process *proc = vec_nth(v->processes, v->processes.length - 1);
  assert(proc->id == proc_id);

  if (options.on_exit.set) {
    lua_pushvalue(L, options.on_exit.value);
    proc->callbacks.on_exit = luaL_ref(L, LUA_REGISTRYINDEX);
  } else {
    proc->callbacks.on_exit = LUA_NOREF;
  }
  if (options.on_stdout.set) {
    lua_pushvalue(L, options.on_stdout.value);
    proc->callbacks.on_stdout = luaL_ref(L, LUA_REGISTRYINDEX);
  } else {
    proc->callbacks.on_stdout = LUA_NOREF;
  }
  if (options.on_stderr.set) {
    lua_pushvalue(L, options.on_stderr.value);
    proc->callbacks.on_stderr = luaL_ref(L, LUA_REGISTRYINDEX);
  } else {
    proc->callbacks.on_stderr = LUA_NOREF;
  }

  proc->stdin_closed = !options.stdin_pipe;

  lua_pushinteger(L, proc_id);
  return 1;
}

static void vv_api_process_write_stdin(struct velvet *v, lua_Integer id, struct u8_slice text) {
  lua_State *L = v->current;
  struct velvet_process *p = check_process(v, id);
  if (p->stdin_closed) bail("Cannot write to process %I: stdin is closed", id);
  velvet_process_write_stdin(v, p, text);
}

static void vv_api_process_close_stdin(struct velvet *v, lua_Integer id) {
  struct velvet_process *p = check_process(v, id);
  velvet_process_close_stdin(v, p);
}

static lua_stackRetCount
vv_api_window_create_process(struct velvet *v, lua_stackIndex cmd,
                             struct velvet_api_window_create_options options) {
  vec_clear(&envlist);
  lua_State *L = v->current;
  struct velvet_window template = {.emulator = vte_default};
  if (options.parent_window.set)
    template.parent_window_id = options.parent_window.value;

  lua_pushvalue(L, cmd); /* push cmd to top of stack */
  if (lua_isstring(L, -1)) {
    if (luaL_len(L, -1) == 0)
      bail("bad argument #1 to 'process_spawn' (string must not be empty)");
    split_and_push_string_array(L);
  }

  if (!lua_istable(L, -1))
    bail("bad argument #1 to 'process_spawn'. string or string[] expected.");

  lua_Integer len = luaL_len(L, -1);
  if (len == 0)
    bail("bad argument #1 to 'process_spawn' (table must not be empty)");

  for (int i = 1; i <= len; i++) {
    lua_geti(L, -1, i);
    if (!lua_isstring(L, -1)) {
      bail("bad argument #1 to 'process_spawn' (table must only contain "
           "strings)");
    }
    const char *arg = luaL_checkstring(L, -1);
    vec_push(&envlist, &arg);
    lua_pop(L, 1);
  }

  /* NULL sentinel for arglist */
  vec_push(&envlist, NULL);

  char **arglist = vec_nth(envlist, 0);
  char *prog = *arglist;
  if (options.working_directory.set)
    string_push_slice(&template.cwd, options.working_directory.value);
  lua_Integer win = velvet_scene_spawn_process_from_template(&v->scene, template, arglist);
  if (win < 0) {
    bail("Error starting %s: %s", prog, strerror(-win));
  }
  lua_pushinteger(L, win);
  return 1;
}

static lua_Integer vv_api_window_create(struct velvet *v, struct velvet_api_window_create_options options) {
  struct velvet_window template = {
      .emulator = vte_default,
      .is_lua_window = true,
  };
  if (options.working_directory.set) string_push_slice(&template.cwd, options.working_directory.value);
  if (options.parent_window.set) template.parent_window_id = options.parent_window.value;
  struct velvet_window *created = velvet_scene_manage(&v->scene, template);
  string_push_format_slow(&created->title, "Untitled %d", created->id);
  return created->id;
}

static bool vv_api_window_is_lua(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = velvet_scene_get_window_from_id(&v->scene, win_id);
  if (w) return w->is_lua_window;
  return false;
}

static void vv_api_window_write(struct velvet *v, lua_Integer win_id, struct u8_slice text) {
  lua_State *L = v->current;
  struct velvet_window *w = check_lua_window(v, win_id);
  if (w->geometry.height == 0 || w->geometry.width == 0) bail("Cannot write to window: size is 0");
  velvet_window_process_output(w, text);
  /* Lua windows should not trigger emulator output. Clear it to be safe just to avoid accumulating buffers. */
  string_clear(&w->emulator_output_buffer);
  /* since lua windows don't have a pty, we also shouldn't allow their input buffer to accumulate. */
  string_clear(&w->emulator.pending_input);
  if (window_visible(v, w)) velvet_invalidate_render(v, "write to window");
}

static void vv_api_client_detach(struct velvet *v, lua_Integer client_id) {
  lua_State *L = v->current;
  struct velvet_client *s;
  vec_find(s, v->clients, s->socket == client_id);
  if (!s) bail("No client exists with socket id %I", client_id);
  velvet_detach_client(v, s, NULL);
}

#define SOCKET_PATH_MAX (int)((sizeof((struct sockaddr_un*)((void*)0))->sun_path) - 1)
static void check_server(struct velvet *v, struct u8_slice server) {
  lua_State *L = v->current;
  string_clear(&stringbuf);
  string_joinpath(&stringbuf, getenv("HOME"), ".local", "share", "velvet", "sockets", (char*)server.content);
  string_ensure_null_terminated(&stringbuf);
  if (strchr((char*)server.content, '/')) bail("Socket name must not contain '/'");
  if (stringbuf.len > SOCKET_PATH_MAX) bail("Socket name too long.");
  string_destroy(&stringbuf);
}

static void vv_api_client_reattach(struct velvet *v, lua_Integer id, struct u8_slice server) {
  lua_State *L = v->current;
  struct velvet_client *s;
  vec_find(s, v->clients, s->socket == id);
  if (!s) bail("No client exists with socket id %I", id);
  check_server(v, server);
  velvet_detach_client(v, s, (char*)server.content);
}

static void vv_api_window_close(struct velvet *v, lua_Integer winid) {
  struct velvet_window *w = check_window(v, winid);
  velvet_scene_close_and_remove_window(&v->scene, w);
}

static struct optional_int vv_api_get_focused_window(struct velvet *v) {
  struct velvet_window *w = velvet_scene_get_focus(&v->scene);
  struct optional_int ret = { .set = w, .value = w ? w->id : 0 };
  return ret;
}

static struct velvet_api_rect vv_api_window_get_geometry(struct velvet *v, lua_Integer winid) {
  struct velvet_api_rect geom = {0};
  struct velvet_window *w = check_window(v, winid);
  struct rect r = w->geometry;
  geom.height = r.height;
  geom.left = r.left + 1;
  geom.top = r.top + 1;
  geom.width = r.width;
  return geom;
}

static void vv_api_window_set_geometry(struct velvet *v, lua_Integer winid, struct velvet_api_rect geometry) {
  struct velvet_window *w = check_window(v, winid);
  /* sanity check -- 1000 is already ridiculous, but let's be lenient */
  if (geometry.width < 0 || geometry.width > 1000 || geometry.height < 0 || geometry.height > 1000) return;
  geometry.left -= 1;
  geometry.top -= 1;
  struct rect new_geometry = { .height = geometry.height, .top = geometry.top, .left = geometry.left, .width = geometry.width};
  if (velvet_window_resize(w, new_geometry, v)) velvet_invalidate_render(v, "window resized");
}

static bool vv_api_window_is_valid(struct velvet *v, struct optional_int winid) {
  if (!winid.set) return false;
  struct velvet_window *w;
  vec_find(w, v->scene.windows, w->id == winid.value);
  return w ? true : false;
}

static lua_stackRetCount vv_api_get_windows(struct velvet *v) {
  lua_State *L = v->current;
  lua_newtable(L);
  lua_Integer index = 1;
  struct velvet_window *w;
  /* hide non-regular windows from the LUA api for now */
  vec_foreach(w, v->scene.windows) {
    lua_pushinteger(L, w->id);
    lua_seti(L, -2, index++);
  }
  return 1;
}

static struct velvet_api_screen_geometry vv_api_get_screen_geometry(struct velvet *v) {
  struct velvet_api_screen_geometry geom = {.height = v->scene.size.height, .width = v->scene.size.width};
  return geom;
}

static void confine_region_to_screen(struct screen *screen, struct velvet_api_rect *region) {
  /* adjust width */
  if (region->left < 0) {
    int delta = -region->left;
    region->left += delta;
    region->width -= delta;
  }
  region->width = CLAMP(region->width, 0, screen->w - region->left);

  /* adjust height */
  /* convert to 0-index */
  /* clamp to scrollback+buffer bounds */
  region->top = CLAMP(region->top, -screen->scroll.height, screen->h - 1);
  /* clamp height to number of available lines */
  region->height = MIN(region->height, screen->h - region->top);
}

static lua_stackRetCount vv_api_window_get_text(struct velvet *v, lua_Integer win_id, struct velvet_api_rect region) {
  lua_State *L = v->current;
  struct velvet_window *w = check_window(v, win_id);
  struct screen *screen = vte_get_current_screen(&w->emulator);

  region.left--;
  region.top--;
  confine_region_to_screen(screen, &region);

  lua_newtable(L); /* line[] */

  struct string scratch = {0};
  for (int row = region.top; row < region.top + region.height; row++) {
    lua_newtable(L); /* { text, wraps, truncated } */
    string_clear(&scratch);
    struct screen_line *l = screen_get_line(screen, row);
    bool wraps = !l->has_newline;
    lua_pushboolean(L, wraps);
    lua_setfield(L, -2, "wraps");
    for (int col = region.left; col < l->eol && col < region.left + region.width; col++) {
      struct screen_cell *c, *p;
      c = &l->cells[col];
      p = col ? c - 1 : NULL;
      if (p && p->cp.is_wide) {
        /* If the left boundary is a wide char, insert a space instead
         * to preserve alignment. */
        string_push_char(&scratch, ' ');
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "truncated");
      } else {
        /* if this cell is wide, increment col to skip the next 0-width cell. */
        string_push_codepoint(&scratch, c->cp.value ? c->cp.value : ' ');
        if (c->cp.is_wide) col++;
      }
    }
    /* todo: string */
    lua_pushlstring(L, (char*)scratch.content, scratch.len);
    lua_setfield(L, -2, "text");
    lua_seti(L, -2, 1 + row - region.top);
  }
  string_destroy(&scratch);
  return 1;
}

static struct velvet_api_mouse_settings vv_api_window_get_mouse_settings(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  struct velvet_api_mouse_settings s = {
      .protocol = (enum velvet_api_mouse_protocol)w->emulator.options.mouse.mode,
      .reporting = (enum velvet_api_mouse_reporting)w->emulator.options.mouse.tracking,
  };
  return s;
}

static void pcall_func_ref(lua_State *L, lua_Integer func_ref) {
  lua_pushcfunction(L, lua_debug_traceback_handler);
  int msgh = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);

  if (lua_pcall(L, 0, 0, msgh) != LUA_OK) {
    struct velvet *v = *(struct velvet **)lua_getextraspace(L);
    struct u8_slice err = luaL_checkslice(L, -1);
    struct velvet_api_system_message_event_args event_args = {
        .level = VELVET_API_SEVERITY_ERROR,
        .message = err,
    };
    velvet_api_raise_system_message(v, event_args);
    lua_pop(L, 1);
  }
  lua_pop(L, 1); // msgh
}

static void schedule_unref(struct schedule_data *d) {
  assert(d->magic == SCHEDULE_MAGIC);
  // unpin function
  luaL_unref(d->state, LUA_REGISTRYINDEX, d->function);
  if (d->state_ref) {
    // unpin coroutine thread
    luaL_unref(d->state, LUA_REGISTRYINDEX, d->state_ref);
  }
}

static void schedule_execute(void *data) {
  struct schedule_data d = *(struct schedule_data*)data;
  free(data);
  pcall_func_ref(d.state, d.function);
  schedule_unref(&d);
}

static bool vv_api_schedule_cancel(struct velvet *v, lua_Integer cancellation_id) {
  struct io_schedule *sched = io_schedule_get(&v->event_loop, cancellation_id);
  if (sched) {
    struct schedule_data *d = sched->data;
    if (d->magic != SCHEDULE_MAGIC) return false;
    schedule_unref(d);
    free(d);
    return io_schedule_cancel(&v->event_loop, cancellation_id);
  }
  return false;
}

static lua_Integer vv_api_schedule_after(struct velvet *v, lua_Integer delay, lua_Integer func) {
  lua_State *L = v->current;
  lua_Integer func_ref, state_ref;
  func_ref = state_ref = 0;

  luaL_checktype(L, func, LUA_TFUNCTION);
  lua_pushvalue(L, func);
  /* if this was called from a coroutine, move the function from the coroutine stack to the main stack */
  if (L != v->L)
    lua_xmove(L, v->L, 1); 
  func_ref = luaL_ref(v->L, LUA_REGISTRYINDEX);
  if (L != v->L) {
    // pin the coroutine thread to ensure it does not get garbage collected
    lua_pushthread(L);
    lua_xmove(L, v->L, 1);
    state_ref = luaL_ref(v->L, LUA_REGISTRYINDEX);
  }

  struct schedule_data *alloc = calloc(1, sizeof(*alloc));
  alloc->magic = SCHEDULE_MAGIC;
  alloc->function = func_ref;
  alloc->state = v->L;
  alloc->state_ref = state_ref;
  return io_schedule(&v->event_loop, delay, schedule_execute, alloc);
}

static void vv_api_debug_set_display_damage(struct velvet *v, bool new_value) {
  velvet_scene_set_display_damage(&v->scene, new_value);
}

static void vv_api_window_paste_text(struct velvet *v, lua_Integer winid, struct u8_slice text) {
  check_window(v, winid);
  velvet_input_paste_text(v, text, winid);
}

static void vv_api_window_send_keys(struct velvet *v, lua_Integer winid, struct u8_slice keys) {
  check_window(v, winid);
  velvet_input_send_keys(v, keys, winid);
}

static void vv_api_set_focused_window(struct velvet *v, lua_Integer winid) {
  struct velvet_window *w = check_window(v, winid);
  if (v->scene.focus != winid) velvet_invalidate_render(v, "focus changed");
  velvet_scene_set_focus(&v->scene, w);
}

static lua_Integer vv_api_get_current_tick(struct velvet *v) {
  (void)v;
  return get_ms_since_startup();
}

static struct u8_slice vv_api_window_get_title(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  struct u8_slice result = {0};
  if (w->title.len) {
    result = u8_slice_from_string(w->title);
  } else if (w->emulator.osc.title.len) {
    result.len = w->emulator.osc.title.len;
    result.content = w->emulator.osc.title.buffer;
  } else if (w->cmdline.len) {
    result = u8_slice_from_string(w->cmdline);
  }
  return result;
}

static void vv_api_window_set_title(struct velvet *v, lua_Integer win_id, struct u8_slice title) {
  struct velvet_window *w = check_window(v, win_id);
  string_clear(&w->title);
  string_push_slice(&w->title, title);
}

static lua_stackRetCount vv_api_get_clients(struct velvet *v) {
  lua_State *L = v->current;
  lua_newtable(L);
  lua_Integer index = 1;
  struct velvet_client *s;
  vec_where(s, v->clients, s->socket && s->output) {
    if (s->socket) {
      lua_pushinteger(L, s->socket);
      lua_seti(L, -2, index++);
    }
  }
  return 1;
}

static void vv_api_set_active_client(struct velvet *v, lua_Integer client_id) {
  lua_State *L = v->current;
  struct velvet_client *s;
  vec_find(s, v->clients, s->socket == client_id);
  if (s == NULL || !s->output) bail("client %I is not a valid client.", client_id);
  velvet_set_focused_client(v, client_id);
}

static lua_Integer vv_api_get_active_client(struct velvet *v) {
  struct velvet_client *s = velvet_get_focused_client(v);
  if (s) return s->socket;
  return 0;
}

static void vv_api_quit(struct velvet *v) {
  v->quit = true;
}

static void vv_api_window_set_hidden(struct velvet *v, lua_Integer win_id, bool hidden) {
  struct velvet_window *w = check_window(v, win_id);
  if (w->hidden != hidden) {
    w->hidden = hidden;
    velvet_invalidate_render(v, "window visibility changed");
  }
}

static bool vv_api_window_get_hidden(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  return w->hidden;
}

static void vv_api_window_set_z_index(struct velvet *v, lua_Integer win_id, lua_Integer z) {
  struct velvet_window *w = check_window(v, win_id);
  if (w->z_index != z) {
    w->z_index = z;
    velvet_invalidate_render(v, "z index changed");
  }
}
static lua_Integer vv_api_window_get_z_index(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  return w->z_index;
}

static float vv_api_window_get_alpha(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  return 1.0 - w->transparency.transparency;
}
static void vv_api_window_set_alpha(struct velvet *v, lua_Integer win_id, float alpha) {
  struct velvet_window *w = check_window(v, win_id);
  alpha = CLAMP(alpha, 0, 1);
  float transparency = 1.0 - alpha;
  if (transparency != w->transparency.transparency) {
    w->transparency.transparency = transparency;
    velvet_invalidate_render(v, "alpha changed");
  }
}

static enum velvet_api_transparency_mode vv_api_window_get_transparency_mode(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  return w->transparency.mode;
}

static void vv_api_window_set_transparency_mode(struct velvet *v, lua_Integer win_id, enum velvet_api_transparency_mode mode) {
  lua_State *L = v->current;
  struct velvet_window *w = check_window(v, win_id);

  if (w->transparency.mode != mode) {
    switch (mode) {
    case VELVET_API_TRANSPARENCY_MODE_NONE:
    case VELVET_API_TRANSPARENCY_MODE_CLEAR:
    case VELVET_API_TRANSPARENCY_MODE_ALL: w->transparency.mode = mode; break;
    default: bail("Invalid transparency mode %I", mode);
    }

    velvet_invalidate_render(v, "transparency mode changed.");
  }
}

static uint8_t fconv(float f) {
  return CLAMP(f * 255, 0, 255);
}
static float iconv(uint8_t v) {
  return (float)v / 255.0f;
}

static struct color rgb_from_palette(struct velvet_api_rgb_color pal) {
  struct color rgb = {
      .kind = VELVET_API_COLOR_KIND_RGB,
      .c.rgb = {
          .r = fconv(pal.red),
          .g = fconv(pal.green),
          .b = fconv(pal.blue),
          .t = pal.alpha.set ? fconv(1.0 - pal.alpha.value) : 0,
      }};
  return rgb;
}

static struct velvet_api_rgb_color palette_from_rgb(struct color col) {
  struct velvet_api_rgb_color api = {
      .red = iconv(col.c.rgb.r),
      .blue = iconv(col.c.rgb.b),
      .green = iconv(col.c.rgb.g),
      .alpha.value = 1.0 - iconv(col.c.rgb.t),
  };
  return api;
}

static struct velvet_api_theme vv_api_get_theme(struct velvet *v) {
  struct velvet_api_theme p = {0};
  p.black = palette_from_rgb(v->scene.theme.palette[0]);
  p.red = palette_from_rgb(v->scene.theme.palette[1]);
  p.green = palette_from_rgb(v->scene.theme.palette[2]);
  p.yellow = palette_from_rgb(v->scene.theme.palette[3]);
  p.blue = palette_from_rgb(v->scene.theme.palette[4]);
  p.magenta = palette_from_rgb(v->scene.theme.palette[5]);
  p.cyan = palette_from_rgb(v->scene.theme.palette[6]);
  p.white = palette_from_rgb(v->scene.theme.palette[7]);
  p.bright_black = palette_from_rgb(v->scene.theme.palette[8]);
  p.bright_red = palette_from_rgb(v->scene.theme.palette[9]);
  p.bright_green = palette_from_rgb(v->scene.theme.palette[10]);
  p.bright_yellow = palette_from_rgb(v->scene.theme.palette[11]);
  p.bright_blue = palette_from_rgb(v->scene.theme.palette[12]);
  p.bright_magenta = palette_from_rgb(v->scene.theme.palette[13]);
  p.bright_cyan = palette_from_rgb(v->scene.theme.palette[14]);
  p.bright_white = palette_from_rgb(v->scene.theme.palette[15]);
  p.foreground = palette_from_rgb(v->scene.theme.foreground);
  p.background = palette_from_rgb(v->scene.theme.background);
  p.cursor_foreground.value = palette_from_rgb(v->scene.theme.cursor.foreground);
  p.cursor_background.value = palette_from_rgb(v->scene.theme.cursor.background);
  return p;
}

static void vv_api_set_theme(struct velvet *v, struct velvet_api_theme new_value) {
  v->scene.theme.palette[0] = rgb_from_palette(new_value.black);
  v->scene.theme.palette[1] = rgb_from_palette(new_value.red);
  v->scene.theme.palette[2] = rgb_from_palette(new_value.green);
  v->scene.theme.palette[3] = rgb_from_palette(new_value.yellow);
  v->scene.theme.palette[4] = rgb_from_palette(new_value.blue);
  v->scene.theme.palette[5] = rgb_from_palette(new_value.magenta);
  v->scene.theme.palette[6] = rgb_from_palette(new_value.cyan);
  v->scene.theme.palette[7] = rgb_from_palette(new_value.white);
  v->scene.theme.palette[8] = rgb_from_palette(new_value.bright_black);
  v->scene.theme.palette[9] = rgb_from_palette(new_value.bright_red);
  v->scene.theme.palette[10] = rgb_from_palette(new_value.bright_green);
  v->scene.theme.palette[11] = rgb_from_palette(new_value.bright_yellow);
  v->scene.theme.palette[12] = rgb_from_palette(new_value.bright_blue);
  v->scene.theme.palette[13] = rgb_from_palette(new_value.bright_magenta);
  v->scene.theme.palette[14] = rgb_from_palette(new_value.bright_cyan);
  v->scene.theme.palette[15] = rgb_from_palette(new_value.bright_white);
  v->scene.theme.foreground = rgb_from_palette(new_value.foreground);
  v->scene.theme.background = rgb_from_palette(new_value.background);
  if (new_value.cursor_foreground.set) {
    v->scene.theme.cursor.foreground = rgb_from_palette(new_value.cursor_foreground.value);
  } else {
    v->scene.theme.cursor.foreground = rgb_from_palette(new_value.background);
  }
  if (new_value.cursor_background.set) {
    v->scene.theme.cursor.background = rgb_from_palette(new_value.cursor_background.value);
  } else {
    v->scene.theme.cursor.background = rgb_from_palette(new_value.foreground);
  }

  if (new_value.bold_bright_colors.set)
    v->scene.theme.bold_bright_colors = new_value.bold_bright_colors.value;
  velvet_invalidate_render(v, "color palette updated");
}

static lua_Integer vv_api_get_fps_target(struct velvet *v) {
  return v->fps_target;
}

static void vv_api_set_fps_target(struct velvet *v, lua_Integer new_value) {
  lua_State *L = v->current;
  if (new_value <= 0) bail("fps target must be a positive integer.");
  v->fps_target = new_value;
}

static float vv_api_window_get_dim_factor(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  return w->dim_factor;
}
static void vv_api_window_set_dim_factor(struct velvet *v, lua_Integer win_id, float factor) {
  struct velvet_window *w = check_window(v, win_id);
  float dim = CLAMP(factor, 0, 1);
  if (dim != w->dim_factor) {
    w->dim_factor = dim;
    velvet_invalidate_render(v, "dim factor changed");
  }
}

static void vv_api_window_send_mouse_move(struct velvet *v, struct velvet_api_mouse_move_event_args mouse_move) {
  check_window(v, mouse_move.win_id);
  velvet_input_send_mouse_move(v, mouse_move);
}

static void vv_api_window_send_mouse_click(struct velvet *v, struct velvet_api_mouse_click_event_args mouse_click) {
  check_window(v, mouse_click.win_id);
  velvet_input_send_mouse_click(v, mouse_click);
}

static void vv_api_window_send_mouse_scroll(struct velvet *v, struct velvet_api_mouse_scroll_event_args mouse_scroll) {
  check_window(v, mouse_scroll.win_id);
  velvet_input_send_mouse_scroll(v, mouse_scroll);
}

static lua_Integer vv_api_window_get_scrollback_size(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  struct screen *active = vte_get_current_screen(&w->emulator);
  return active->scroll.height;
}
static lua_Integer vv_api_window_get_scroll_offset(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  struct screen *active = vte_get_current_screen(&w->emulator);
  return active->scroll.view_offset;
}

static void vv_api_window_set_scroll_offset(struct velvet *v, lua_Integer win_id, lua_Integer scroll_offset) {
  struct velvet_window *w = check_window(v, win_id);
  struct screen *active = vte_get_current_screen(&w->emulator);
  if (screen_set_scroll_offset(active, scroll_offset)) {
    if (window_visible(v, w)) {
      velvet_invalidate_render(v, "Scroll offset changed.");
    }
  }
}

static void vv_api_window_set_drawing_color(struct velvet *v,
                                     lua_Integer win_id,
                                     enum velvet_api_brush brush,
                                     struct velvet_api_rgb_color color) {
  struct velvet_window *w = check_window(v, win_id);
  struct color col = rgb_from_palette(color);
  struct screen *g = vte_get_current_screen(&w->emulator);
  switch (brush) {
  case VELVET_API_BRUSH_BACKGROUND: g->cursor.brush.bg = col; break;
  case VELVET_API_BRUSH_FOREGROUND: g->cursor.brush.fg = col; break;
  }
}

static struct velvet_api_coordinate vv_api_window_get_cursor_position(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  struct screen *g = vte_get_current_screen(&w->emulator);
  return (struct velvet_api_coordinate){
      .col = g->cursor.column + 1,
      .row = g->cursor.line + 1,
  };
}

static void vv_api_window_set_cursor_position(struct velvet *v, lua_Integer win_id, struct velvet_api_coordinate pos) {
  struct velvet_window *w = check_lua_window(v, win_id);
  struct screen *g = vte_get_current_screen(&w->emulator);
  pos.col = CLAMP(pos.col, 1, w->geometry.width);
  pos.row = CLAMP(pos.row, 1, w->geometry.height);

  if (w->emulator.options.cursor.visible && (pos.col != g->cursor.column || pos.row != g->cursor.line))
    velvet_invalidate_render(v, "cursor moved");

  screen_set_cursor_position(g, pos.col - 1, pos.row - 1);
}

static struct u8_slice vv_api_window_get_working_directory(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  if (w->pty && platform.get_cwd_from_pty) {
    char buf[256] = {0};
    if (platform.get_cwd_from_pty(w->pty, buf, sizeof(buf))) {
      string_clear(&w->cwd);
      string_push_cstr(&w->cwd, buf);
    }
  }
  return u8_slice_from_string(w->cwd);
}

static char get_process_foreground_buffer[256] = {0};
static struct u8_slice vv_api_window_get_foreground_process_name(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_process_window(v, win_id);
  if (w->pty && platform.get_process_from_pty) {
    if (platform.get_process_from_pty(w->pty, get_process_foreground_buffer, sizeof(get_process_foreground_buffer))) {
      return u8_slice_from_cstr(get_process_foreground_buffer);
    }
  }
  return (struct u8_slice){0};
}

static void vv_api_window_set_parent(struct velvet *v, lua_Integer win_id, struct optional_int parent) {
  struct velvet_window *w1 = check_window(v, win_id);
  if (parent.set) { 
    check_window(v, parent.value);
    w1->parent_window_id = parent.value;
  } else {
    w1->parent_window_id = 0;
  }
}

static struct optional_int vv_api_window_get_parent(struct velvet *v, lua_Integer win_id) {
  struct velvet_window *w = check_window(v, win_id);
  int parent = w->parent_window_id;
  struct optional_int result = {0};
  if (parent) {
    result.set = true;
    result.value = parent;
  }
  return result;
}

static struct u8_slice vv_api_get_startup_directory(struct velvet *v) {
  return u8_slice_from_cstr(v->startup_directory);
}

static void vv_api_client_set_options(struct velvet *v, lua_Integer client_id, struct velvet_api_client_options options) {
  lua_State *L = v->current;
  struct velvet_client *s;
  /* bit of a hack because clients don't really have a way of knowing their own id */
  if (client_id == 0) client_id = v->socket_cmd_sender;
  vec_find(s, v->clients, s->socket == client_id);
  if (s == NULL) bail("client %I is not a valid client.", client_id);
  s->ws.height = options.lines;
  s->ws.width = options.columns;
  s->ws.x_pixel = options.x_pixel;
  s->ws.y_pixel = options.y_pixel;
  velvet_force_full_redraw(v);
}

static void vv_api_window_send_raw_key(struct velvet *v, lua_Integer win_id, struct velvet_api_window_key_event key) {
  velvet_input_send_key_to_window(v, key, check_window(v, win_id));
}

static bool file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static bool read_file(struct string *str, char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return false;

  fseek(f, 0, SEEK_END);
  size_t len = ftell(f);
  fseek(f, 0, SEEK_SET);
  string_ensure_capacity(str, len);
  fread(str->content, 1, len, f);
  str->len = len;
  fclose(f);
  return true;
}

/* validate the user's config. If this fails, an appropriate lua error will be thrown.
 * Normal return indicates success */
static void check_config(struct velvet *v) {
  lua_State *L = v->current;
  struct string path = {0};
  string_joinpath(&path, getenv("HOME"), ".config/velvet/init.lua");
  string_ensure_null_terminated(&path);

  /* config did not exist -- just return */
  if (!file_exists((char*)path.content)) {
    string_destroy(&path);
    return;
  }

  struct string config = {0};
  bool ok = read_file(&config, (char*)path.content);
  string_destroy(&path);

  if (!ok) bail("Unable to open config for reading.");

  int status = luaL_loadbuffer(v->L, (char*)config.content, config.len, "@init.lua");
  string_destroy(&config);
  if (status != LUA_OK) {
    const char *s = luaL_checkstring(v->L, -1);
    /* raise the config error to the caller if loadbuffer failed */
    bail("Error parsing config: %s", s);
  }
  /* pop the loadbuffer() chunk */
  lua_pop(v->L, 1);
}

static void vv_api_reload(struct velvet *v) {
  if (v->reloading) return;
  /* reject the reload request if the config has syntax errors */
  check_config(v);

  /* raise reload event to allow modules to store important state */
  struct velvet_api_pre_reload_event_args args = {.time = get_ms_since_startup()};
  velvet_api_raise_pre_reload(v, args);

  /* we need to unwind the stack before reloading because
   * it is not possible to return to the lua vm after closing it.
   * Otherwise we would return into an invalid lua context.
   *
   * Setting the reload flag also poisons the API so all
   * vv.api functions will fail with an error, hopefully causing
   * the lua stack to unwind asap if callers are doing naughty
   * things after calling reload. */
  v->reloading = true;

  /* break the main dispatcher loop after reloading.
   * this prevents other cli actions from spuriously failing because they
   * were unfortunate enough to be handled while the lua vm is in an invalid state.
   * By breaking the main dispatcher loop, velvet can boot up a new vm before handling pending requests. */
  v->event_loop.dispatch_break = true;

  string_destroy(&stringbuf);
  vec_destroy(&envlist);
}

static lua_Integer vv_api_string_display_width(struct velvet *v, struct u8_slice string) {
  lua_State *L = v->current;
  lua_Integer result = 0;
  struct u8_slice_codepoint_iterator it = {.src = string};
  while (u8_slice_codepoint_iterator_next(&it)) {
    result += utf8proc_charwidth(it.current.value);
  }
  if (it.reject) bail("Could not determine display width of '%s': Invalid utf8 sequence.", string.content);
  return result;
}

static struct u8_slice vv_api_string_lower(struct velvet *v, struct u8_slice string) {
  lua_State *L = v->current;
  struct string *s = &stringbuf;
  string_clear(s);
  struct u8_slice_codepoint_iterator it = { .src = string };
  while (u8_slice_codepoint_iterator_next(&it)) {
    uint32_t cp = utf8proc_tolower(it.current.value);
    string_push_codepoint(s, cp);
  }
  if (it.reject) bail("Could not lower '%s': Invalid utf8 sequence.", string.content);
  return string_as_u8_slice(*s);
}

static struct u8_slice vv_api_string_upper(struct velvet *v, struct u8_slice string) {
  lua_State *L = v->current;
  struct string *s = &stringbuf;
  string_clear(s);
  struct u8_slice_codepoint_iterator it = { .src = string };
  while (u8_slice_codepoint_iterator_next(&it)) {
    uint32_t cp = utf8proc_toupper(it.current.value);
    string_push_codepoint(s, cp);
  }
  if (it.reject) bail("Could not upper '%s': Invalid utf8 sequence.", string.content);
  return string_as_u8_slice(*s);
}

static bool needs_quote(const char *ch) {
  if (*ch == 0) return true;
  if (isdigit(*ch)) return true;
  for (; *ch; ch++)
    if (!isalnum(*ch) && *ch != '_') return true;
  return false;
}

void string_get_lua_quote_pair(const char *ch, char **left, char **right) {
  const char *tmp = ch;
  bool has_dquot, has_squot;
  has_dquot = has_squot = false;
  for (; *tmp; tmp++) {
    has_dquot |= *tmp == '"';
    has_squot |= *tmp == '\'';
  }

  if (has_squot && has_dquot) {
    if (!strstr(ch, "]]")) {
      *left = "[[";
      *right = "]]";
    } else if (!strstr(ch, "]=]")) {
      *left = "[=[";
      *right = "]=]";
    } else if (!strstr(ch, "]==]")) {
      *left = "[==[";
      *right = "]==]";
    } else if (!strstr(ch, "]===]")) {
      *left = "[===[";
      *right = "]===]";
    } else if (!strstr(ch, "]====]")) {
      *left = "[====[";
      *right = "]====]";
    } else {
      /* treat this as an adversarial example and emit nil. */
      *left = *right = NULL;
    }
  } else if (has_dquot) {
    *left = "'";
    *right = "'";
  } else {
    *left = "\"";
    *right = "\"";
  }
}

static void get_key_quote(const char *ch, char **left, char **right) {
  if (!needs_quote(ch)) {
    *left = "";
    *right = "";
    return;
  }

  const char *tmp = ch;
  bool has_dquot, has_squot;
  has_dquot = has_squot = false;
  for (; *tmp; tmp++) {
    has_dquot |= *tmp == '"';
    has_squot |= *tmp == '\'';
  }

  if (has_squot && has_dquot) {
    if (!strstr(ch, "]]")) {
      *left = "[ [[";
      *right = "]] ]";
    } else if (!strstr(ch, "]=]")) {
      *left = "[ [=[";
      *right = "]=] ]";
    } else if (!strstr(ch, "]==]")) {
      *left = "[ [==[";
      *right = "]==] ]";
    } else {
      /* treat this as an adversarial example and emit nil. */
      *left = *right = NULL;
    }
  } else if (has_dquot) {
    *left = "['";
    *right = "']";
  } else {
    *left = "[\"";
    *right = "\"]";
  }
}

/* TODO:
 * Two-pass table iteration -- nice-to-have, get rid of superfluous indexers
 * First pass stores consecutive keys (no [1] = x, [2] = y, ...)
 * Second pass stores explicit keys
 *
 * Recursive references -- essential
 * If a table references a parent table, we need to store the parent table in a local,
 * and then assign those fields after construction.
 *
 * Table instance aliasing -- won't implement unless a use case emerges. This requires topologically sorting
 * and constructing tables according to their dependencies.
 * If there are multiple instances of the same table, we need to
 * instantiate the table in local and then reference that local.
 *
 * Expanded string quoting -- won't implement, consider strings simultaneously containing ', ", ]], ]=], ]==] as
 * adversarial.
 *
 * Storing functions -- won't implement, this is not practical.
 */

static void string_push_cstr_escaped(struct string *s, const char *cstr) {
  for (const char *ch = cstr; *ch; ch++) {
    switch (*ch) {
    case '\\': string_push_cstr(s, "\\\\"); break;
    case '\n': string_push_cstr(s, "\\n"); break;
    case '\t': string_push_cstr(s, "\\t"); break;
    case '\r': string_push_cstr(s, "\\r"); break;
    default: string_push_char(s, *ch); break;
    }
  }
}

struct emit_context {
  struct vec /* lua_pointer */ recursion_guard;
  struct string output;
  int indent;
};

static bool emit_table(lua_State *L, struct emit_context *ctx);
static bool emit_literal(lua_State *L, struct emit_context *ctx) {
  char buf[64];
  /* emit literal */
  switch (lua_type(L, -1)) {
  case LUA_TBOOLEAN: {
    bool b = lua_toboolean(L, -1);
    string_push_format_slow(&ctx->output, "%s", b ? "true" : "false");
  } break;
  case LUA_TNUMBER: {
    if (lua_isinteger(L, -1)) {
      lua_Integer i = lua_tointeger(L, -1);
      string_push_format_slow(&ctx->output, "%lld", i);
    } else {
      lua_Number n = lua_tonumber(L, -1);
      if (isnan(n)) {
        string_push_cstr(&ctx->output, "0 / 0 --[[ nan ]]");
      } else if (isinf(n)) {
        string_push_cstr(&ctx->output, n > 0 ? "math.huge" : "-math.huge");
      } else {
        snprintf(buf, sizeof(buf), "%.14g", n);
        char *postfix = (strchr(buf, '.') || strchr(buf, 'e')) ? "" : ".0";
        string_push_format_slow(&ctx->output, "%s%s", buf, postfix);
      }
    }
  } break;
  case LUA_TSTRING: {
    const char *value = lua_tostring(L, -1);
    char *lq, *rq;
    string_get_lua_quote_pair(value, &lq, &rq);
    if (lq && rq) {
      string_push_cstr(&ctx->output, lq);
      string_push_cstr_escaped(&ctx->output, value);
      string_push_cstr(&ctx->output, rq);
    } else {
      string_push_cstr(&ctx->output, "nil");
    }
  } break;
  case LUA_TTABLE: {
    emit_table(L, ctx);
  } break;
  default: return false;
  }
  return true;
}

static bool emit_table(lua_State *L, struct emit_context *ctx) {
  assert(lua_type(L, -1) == LUA_TTABLE);
  const void *handle = lua_topointer(L, -1);
  void **duplicate;
  vec_find(duplicate, ctx->recursion_guard, handle == *duplicate);
  if (duplicate) {
    string_push_cstr(&ctx->output, "nil");
    /* todo: deferred assignment */
    return true;
  }
  size_t idx = ctx->recursion_guard.length;
  vec_push(&ctx->recursion_guard, &handle);

  string_push_format_slow(&ctx->output, "%s", "{");
  ctx->indent++;

  lua_pushnil(L);
  bool anyKeys = false;
  while (lua_next(L, -2) != 0) {
    /* emit key */
    switch (lua_type(L, -2)) {
    case LUA_TBOOLEAN: {
      bool b = lua_toboolean(L, -2);
      string_push_format_slow(&ctx->output, "\n%*s[%s] = ", ctx->indent * 2, "", b ? "true" : "false");
    } break;
    case LUA_TNUMBER: {
      if (lua_isinteger(L, -2)) {
        lua_Integer i = lua_tointeger(L, -2);
        string_push_format_slow(&ctx->output, "\n%*s[%lld] = ", ctx->indent * 2, "", i);
      } else {
        lua_Number n = lua_tonumber(L, -2);
        /* nan is not a valid table key because nan ~= nan, so we don't need to handle that case */
        if (isinf(n)) {
          string_push_format_slow(
              &ctx->output, "\n%*s[%s] = ", ctx->indent * 2, "", n > 0 ? "math.huge" : "-math.huge");
        } else {
          /* numbers with an integer representation are coerced during insertion, so
           * we don't need to handle decimal insertion for numeric keys. */
          string_push_format_slow(&ctx->output, "\n%*s[%.14g] = ", ctx->indent * 2, "", n);
        }
      }
    } break;
    case LUA_TSTRING: {
      const char *key = lua_tostring(L, -2);
      char *lq, *rq;
      get_key_quote(key, &lq, &rq);
      if (lq && rq) {
        string_push_format_slow(&ctx->output, "\n%*s", ctx->indent * 2, "");
        string_push_cstr(&ctx->output, lq);
        string_push_cstr_escaped(&ctx->output, key);
        string_push_cstr(&ctx->output, rq);
        string_push_cstr(&ctx->output, " = ");
      } else {
        /* adversarial string key silently skipped */
        lua_pop(L, 1);
        continue;
      }
    } break;
    default: {
      /* unsupported key type silently skipped */
      lua_pop(L, 1);
      continue;
    }
    }
    anyKeys = true;

    emit_literal(L, ctx);
    string_push_char(&ctx->output, ',');
    lua_pop(L, 1); // pop value
  }
  vec_remove_at(&ctx->recursion_guard, idx);
  ctx->indent--;
  char *push = ctx->indent ? "}" : "}"; /* avoid adding a trailing comma to the outermost object */
  if (anyKeys) {
    string_push_format_slow(&ctx->output, "\n%*s%s", ctx->indent * 2, "", push);
  } else {
    string_push_format_slow(&ctx->output, "%s", push);
  }

  return true;
}

static void velvet_store_string(struct velvet *v, struct u8_slice key, struct u8_slice value) {
  struct velvet_kvp *it = NULL;
  vec_find(it, v->stored_strings, u8_slice_equals(key, string_as_u8_slice(it->key)));
  if (it == NULL) it = vec_new_element(&v->stored_strings);
  string_clear(&it->key);
  string_push_slice(&it->key, key);
  string_clear(&it->value);
  string_push_slice(&it->value, value);
}

static lua_stackRetCount vv_api_runtime_store_value(struct velvet *v, struct u8_slice name, lua_stackIndex value) {
  lua_State *L = v->current;
  struct emit_context ctx = {.recursion_guard = vec(void *)};
  string_push_cstr(&ctx.output, "return ");
  lua_pushvalue(L, value); /* push value to top for emit_literal */
  emit_literal(L, &ctx);
  lua_pop(L, 1);
  string_ensure_null_terminated(&ctx.output);
  velvet_store_string(v, name, string_as_u8_slice(ctx.output));
  string_destroy(&ctx.output);
  vec_destroy(&ctx.recursion_guard);
  return 0;
}

static lua_stackRetCount vv_api_runtime_load_value(struct velvet *v, struct u8_slice name) {
  lua_State *L = v->current;
  struct velvet_kvp *it = NULL;
  vec_find(it, v->stored_strings, u8_slice_equals(name, string_as_u8_slice(it->key)));
  if (it == NULL) return 0;
  string_ensure_null_terminated(&it->value);
  if (luaL_loadbuffer(L, (char *)it->value.content, it->value.len, "@velvet.runtime_load") != LUA_OK) {
    lua_error(L);
  }
  lua_call(L, 0, 1);
  // return value from lua_call
  return 1;
}

static void vv_api_clipboard_set(struct velvet *v, struct u8_slice text) {
  struct string osc_buffer = {0};
  /* OSC 52 sets the clipboard */
  string_push_cstr(&osc_buffer, "\x1b]52;c;");
  u8_slice_encode_base64(text, &osc_buffer);
  string_push_char(&osc_buffer, '\a');
  /* in almost all cases there will be just 1 client, but let's just push to all
    * and hope one of them handles OSC 52 */
  struct velvet_client *s;
  vec_where(s, v->clients, s->input && s->output) {
    string_push_string(&s->pending_output, osc_buffer);
  }
  string_destroy(&osc_buffer);
}

static lua_Integer vv_api_get_scrollback_scroll_multiplier(struct velvet *v) {
  return v->input.options.scroll_multiplier;
}

static void vv_api_set_scrollback_scroll_multiplier(struct velvet *v, lua_Integer new_value) {
  v->input.options.scroll_multiplier = new_value;
}

static struct velvet_api_coordinate vv_api_get_mouse_position(struct velvet *v) {
  return v->input.last_mouse_position;
}

static lua_stackRetCount vv_api_get_servernames(struct velvet *v) {
  lua_State *L = v->current;
  string_clear(&stringbuf);
  string_joinpath(&stringbuf, getenv("HOME"), ".local", "share", "velvet", "sockets");
  string_ensure_null_terminated(&stringbuf);
  lua_newtable(L);
  DIR *dir = opendir((char *)stringbuf.content);
  if (!dir) return 1;

  struct dirent *entry;
  int index = 1;
  while ((entry = readdir(dir)) != NULL) {
    const char *name = entry->d_name;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    lua_pushstring(L, name);
    lua_seti(L, -2, index++);
  }

  closedir(dir);

  return 1;
}

static struct u8_slice vv_api_get_servername(struct velvet *v) {
  (void)v;
  return u8_slice_from_cstr(getenv("VELVET"));
}

static void lua_pushcolor(lua_State *L, struct color col) {
  lua_newtable(L); /* cell_color */
  if (col.kind == VELVET_API_COLOR_KIND_RGB) {
    lua_newtable(L); /* rgb_color */
    struct velvet_api_rgb_color api_color = palette_from_rgb(col);
    lua_pushnumber(L, api_color.red);
    lua_setfield(L, -2, "red");
    lua_pushnumber(L, api_color.green);
    lua_setfield(L, -2, "green");
    lua_pushnumber(L, api_color.blue);
    lua_setfield(L, -2, "blue");
    lua_pushnumber(L, api_color.alpha.value);
    lua_setfield(L, -2, "alpha");
    lua_setfield(L, -2, "rgb"); /* cell_color[rgb] = rgb */
  } else if (col.kind == VELVET_API_COLOR_KIND_TABLE) {
    lua_pushinteger(L, col.c.table);
    lua_setfield(L, -2, "table"); /* cell_color[table] = col.c.table */
  } else {
    /* return empty table to indicate no color */
  }
}

/* return: cell_line[]
 * {
 * { cells: cell { content, style, foreground, background }, wraps },
 * ...
 * }
 */

static lua_stackRetCount vv_api_window_get_cells(struct velvet *v, lua_Integer win_id, struct velvet_api_rect region) {
  uint8_t decode_buf[4] = {0};
  lua_State *L = v->current;
  struct velvet_window *w = check_window(v, win_id);
  struct screen *screen = vte_get_current_screen(&w->emulator);

  region.left--;
  region.top--;
  confine_region_to_screen(screen, &region);

  /* cell_lines: cell_line[] */
  lua_newtable(L);
  int line_idx = 1;
  for (int row = region.top; row < region.top + region.height; row++) {
    struct screen_line *l = screen_get_line(screen, row);
    /* cell_line: { cells: cell[], wraps: bool } */
    lua_newtable(L);
    lua_pushboolean(L, !l->has_newline);
    lua_setfield(L, -2, "wraps");
    lua_newtable(L); /* cell[] */
    int cell_idx = 1;
    for (int col = region.left; col < l->eol && col < region.left + region.width; col++) {
      struct screen_cell *c, *p;
      c = &l->cells[col];
      p = col ? c - 1 : NULL;
      lua_newtable(L); /* cell */
      if (p && p->cp.is_wide) {
        /* if the previous cell is wide, this cell should be {} */
        lua_seti(L, -2, cell_idx++);
        continue;
      }
      { /* cell[content] = cp.value */
        uint32_t cp = c->cp.value;
        if (!cp) cp = ' ';
        int n = codepoint_to_utf8(cp, decode_buf);
        lua_pushlstring(L, (char *)decode_buf, n);
        lua_setfield(L, -2, "content");
      }
      { /* cell[background] = c->style.bg */
        lua_pushcolor(L, c->style.bg);
        lua_setfield(L, -2, "background");
      }
      { /* cell[foreground] = c->style.fg */
        lua_pushcolor(L, c->style.fg);
        lua_setfield(L, -2, "foreground");
      }
      { /* cell[style] = c->style.attr */
        lua_pushinteger(L, c->style.attr);
        lua_setfield(L, -2, "style");
      }

      lua_seti(L, -2, cell_idx++); /* cells[col] = cell */
    }
    lua_setfield(L, -2, "cells"); /* cell_line.cells = cells */
    lua_seti(L, -2, line_idx++); /* cell_lines[line_idx] = cell_line */
  }
  return 1;
}

static lua_Integer vv_api_get_environment(struct velvet *v) {
  extern char **environ;
  lua_State *L = v->current;
  lua_newtable(L);

  for (char **env = environ; *env != NULL; env++) {
    char *entry = *env;
    char *eq = strchr(entry, '=');

    /* Skip malformed entries. */
    if (eq == NULL || eq == entry) continue;

    lua_pushlstring(L, entry, eq - entry);
    lua_pushstring(L, eq + 1);
    lua_settable(L, -3);
  }

  return 1;
}

#include "velvet_lua_bindings.c"
int luaopen_velvet_api(lua_State *L) {
  luaL_newlib(L, velvet_lua_function_table);
  return 1;
}
