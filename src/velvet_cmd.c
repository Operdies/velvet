#include "velvet.h"
#include "lauxlib.h"
#include "utils.h"
#include <errno.h>
#include "velvet_lua.h"

static int l_socket_print(lua_State *L) {
  struct velvet *v = *(struct velvet **)lua_getextraspace(L);
  struct velvet_coroutine *ctx;
  int source_socket = lua_tointeger(L, lua_upvalueindex(1));
  vec_find(ctx, v->coroutines, ctx->socket == source_socket);
  if (!ctx) return 0;

  if (source_socket == 0) return 0;

  int out_stream = luaL_checkinteger(L, 1);
  struct u8_slice msg = luaL_checkslice(L, 2);

  struct string *out_buffer = out_stream == 1 ? &ctx->pending_output : &ctx->pending_error;
  string_push_slice(out_buffer, msg);
  string_push_char(out_buffer, '\n');
  return 0;
}

static int l_coroutine_setup(lua_State *co) {
  struct velvet *v = *(struct velvet **)lua_getextraspace(co);
  struct velvet_coroutine *ctx;
  int source_socket = lua_tointeger(co, lua_upvalueindex(1));
  vec_find(ctx, v->coroutines, ctx->socket == source_socket);
  if (!ctx) return 0;
  ctx->coroutine = co;
  /* print functions are set up on the lua side */
  return 0;
}

static void coroutine_cleanup(lua_State *co) {
  /* cancelling the coroutine does two things:
   * 1. Triggers any functions registered with vv.async.defer()
   * 2. Removes the coroutine from the resume table in vv.async,
   * preventing it from being resumed. Hopefully later garbage collected. */

  // vv.async.cancel(co)
  lua_getglobal(co, "vv");
  lua_getfield(co, -1, "async");
  lua_getfield(co, -1, "cancel");
  lua_pushthread(co);
  lua_call(co, 1, 0);
  lua_pop(co, 2); /* pop async, vv */

  // COROUTINE_PRINT[co] = nil
  lua_getglobal(co, "COROUTINE_PRINT");
  lua_pushthread(co);
  lua_pushnil(co);
  lua_settable(co, -3);
  lua_pop(co, 1); /* pop COROUTINE_PRINT */

  // COROUTINE_ARGS[co] = nil
  lua_getglobal(co, "COROUTINE_ARGS");
  lua_pushthread(co);
  lua_pushnil(co);
  lua_settable(co, -3);
  lua_pop(co, 1); /* pop COROUTINE_ARGS */
}

static int l_coroutine_cleanup(lua_State *co) {
  struct velvet *v = *(struct velvet **)lua_getextraspace(co);
  struct velvet_coroutine *ctx;
  vec_find(ctx, v->coroutines, ctx->coroutine == co);

  coroutine_cleanup(co);

  if (ctx) {
    bool ok = lua_toboolean(co, 1);
    /* indicate this coroutine is done and the socket can be closed after flushing */
    ctx->coroutine = NULL;
    ctx->status = ok ? VELVET_COROUTINE_SUCCESS : VELVET_COROUTINE_ERROR;
  }

  return 0;
}

void velvet_lua_execute_chunk(struct velvet *v, struct u8_slice chunk, int source_socket, struct velvet_lua_varargs args) {
  struct velvet_coroutine *ctx;
  vec_find(ctx, v->coroutines, ctx->socket == source_socket);

  lua_rawgeti(v->L, LUA_REGISTRYINDEX, v->coroutine_wrapper_function);
  if (luaL_loadbuffer(v->L, (char *)chunk.content, chunk.len, "=(lua cmd)") != LUA_OK) {
    struct u8_slice err = luaL_checkslice(v->L, -1);
    if (ctx) {
      string_push_slice(&ctx->pending_error, err);
      ctx->status = VELVET_COROUTINE_SYNTAX_ERROR;
    } else {
      velvet_log("lua cmd error: %.*s", (int)err.len, err.content);
    }
  } else {
    lua_pushinteger(v->L, source_socket);
    lua_pushcclosure(v->L, l_coroutine_setup, 1);
    lua_pushcfunction(v->L, l_coroutine_cleanup);
    lua_pushinteger(v->L, source_socket);
    lua_pushcclosure(v->L, l_socket_print, 1);

    int num_args = 4; /* chunk, setup, cleanup, print_function, ... */
    for (int i = 0; i < args.n; i++) {
      lua_pushstring(v->L, args.args[i]);
      num_args++;
    }
    lua_Integer status = lua_pcall(v->L, num_args, 0, 0);
    if (status != LUA_OK) {
      const char *err = lua_tostring(v->L, -1);
      velvet_log("pcall: %s", err);
      if (ctx) { 
        string_push_cstr(&ctx->pending_error, err);
        ctx->status = VELVET_COROUTINE_ERROR;
      }
    }
  }
  lua_pop(v->L, lua_gettop(v->L));
}

static int read_digit(struct u8_slice s, int *i32) {
  if (s.len == 0) return false;
  size_t i, v;
  i = v = 0;
  for (; i < s.len; i++) {
    uint8_t ch = s.content[i];
    if (!(ch >= '0' && ch <= '9')) break;
    v *= 10;
    v += ch - '0';
  }
  *i32 = v;
  return i;
}

void velvet_cmd(struct velvet *v, int source_socket, struct u8_slice cmd) {
  int codelength;
  int read = read_digit(cmd, &codelength);
  if (!read) {
    io_write(source_socket, u8_slice_from_cstr("Expected command to start with length encoding"));
    return;
  }

  v->socket_cmd_sender = source_socket;
  const uint8_t *chunk_start = cmd.content + read;

  size_t remaining = (cmd.content + cmd.len) - chunk_start;
  if (remaining != (size_t)codelength) {
    io_write_format_slow(source_socket, "lua chunk length does not match encoded length. "
                         "This could be due to a partial write (chunk too large),"
                         "or it could be a bug!");
  } else {
    struct u8_slice chunk = {.len = codelength, .content = chunk_start};
    struct velvet_lua_varargs args = {0};
    velvet_lua_execute_chunk(v, chunk, source_socket, args);
  }
}

void velvet_coroutine_destroy(struct velvet *velvet, struct velvet_coroutine *co) {
  if (co->coroutine) coroutine_cleanup(co->coroutine);
  if (co->socket) {
    /* ignore errors */
    write(co->socket, &co->status, sizeof(co->status));
    close(co->socket);
  }
  if (co->out_fd) close(co->out_fd);
  if (co->err_fd) close(co->err_fd);
  string_destroy(&co->pending_output);
  string_destroy(&co->pending_error);
  *co = (struct velvet_coroutine){0};
  size_t idx = vec_index(&velvet->coroutines, co);
  vec_remove_at(&velvet->coroutines, idx);
}
