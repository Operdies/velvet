#include "collections.h"
#include "platform.h"
#include "utils.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include "velvet_alloc.h"
#include "velvet_lua.h"
#include "velvet_scene.h"
#include "velvet_process.h"
#include <sys/resource.h>
#include "csi.h"

static bool exit_on_failure = true;

static int n_failures = 0;

static void fail(void) {
  n_failures++;
  if (exit_on_failure) __builtin_trap();
}

static void assert_eq(int actual, int expected, const char *test_name, const char *msg) {
  if (actual == expected) return;
  printf("Assertion failed: %d == %d (%s: %s)\n", actual, expected, test_name, msg);
  fail();
}

void assert_csi_param_equals(const char *testname, struct csi_param *expected, struct csi_param *actual, int index) {
  char indexbuf[50];
  snprintf(indexbuf, 30, "Primary %d", index);
  assert_eq(expected->primary, actual->primary, testname, indexbuf);
  for (int i = 0; i < (int)LENGTH(expected->sub); i++) {
    snprintf(indexbuf, 30, "Primary %d sub %d", index, i);
    assert_eq(expected->sub[i], actual->sub[i], testname, indexbuf);
  }
}

void assert_csi_equals(const char *testname, struct csi *expected, struct csi *actual) {
  assert_eq(expected->state, actual->state, testname, "states differ");
  assert_eq(expected->final, actual->final, testname, "final byte differs");
  assert_eq(expected->intermediate, actual->intermediate, testname, "intermediate byte differs");
  assert_eq(expected->prefix, actual->prefix, testname, "prefix byte differs");
  assert_eq(expected->n_params, actual->n_params, testname, "n_params differs");

  for (int i = 0; i < expected->n_params; i++) {
    struct csi_param *exp = &expected->params[i];
    struct csi_param *act = &actual->params[i];
    assert_csi_param_equals(testname, exp, act, i);
  }
}

void test_csi_testcase(const char *testname, char *input, struct csi expected) {
  struct u8_slice input_slice = u8_slice_from_cstr((char *)input);
  struct csi actual = {0};
  size_t count = csi_parse(&actual, input_slice);
  if (expected.state == CSI_ACCEPT) assert(count == input_slice.len);
  assert_csi_equals(testname, &expected, &actual);
}

void test_csi_parsing(void) {
  test_csi_testcase("Reject Empty", "", (struct csi){.state = CSI_REJECT});
  test_csi_testcase(
      "Reset 1", "0m", (struct csi){.state = CSI_ACCEPT, .final = 'm', .n_params = 1, .params = {{.primary = 0}}});
  test_csi_testcase(
      "Reset 0", "m", (struct csi){.state = CSI_ACCEPT, .final = 'm', .n_params = 0, .params = {{.primary = 0}}});
  test_csi_testcase("Basic Parameter List",
                    "1;2;33;444m",
                    (struct csi){
                        .final = 'm',
                        .n_params = 4,
                        .state = CSI_ACCEPT,
                        .params = {{.primary = 1}, {.primary = 2}, {.primary = 33}, {.primary = 444}},
                    });
  test_csi_testcase(
      "RGB Modern Syntax",
      "38:2:100:100:100m",
      (struct csi){
          .final = 'm', .n_params = 1, .state = CSI_ACCEPT, .params = {{.primary = 38, .sub = {2, 100, 100, 100}}}});
  test_csi_testcase(
      "RGB Modern Syntax + colorspace",
      "38:2::100:100:100m",
      (struct csi){
          .final = 'm', .n_params = 1, .state = CSI_ACCEPT, .params = {{.primary = 38, .sub = {2, 0, 100, 100, 100}}}});
  test_csi_testcase(
      "RGB Legacy Syntax",
      "38;2;100;100;100m",
      (struct csi){
          .final = 'm', .n_params = 1, .state = CSI_ACCEPT, .params = {{.primary = 38, .sub = {2, 100, 100, 100}}}});
  test_csi_testcase(
      "RGB Legacy Syntax 2",
      "48;2;118;159;240;38;2;235;160;172m",
      (struct csi){.final = 'm',
                   .n_params = 2,
                   .state = CSI_ACCEPT,
                   .params = {{.primary = 48, .sub = {2, 118, 159, 240}}, {.primary = 38, .sub = {2, 235, 160, 172}}}});
  test_csi_testcase("Test prefix / intermediate parsing 1",
                    ">c",
                    (struct csi){.prefix = '>', .final = 'c', .state = CSI_ACCEPT, .n_params = 0});
  test_csi_testcase("Test prefix / intermediate parsing 1",
                    ">?c",
                    (struct csi){.prefix = '>', .final = 'c', .state = CSI_ACCEPT, .n_params = 0, .intermediate = '?'});
  test_csi_testcase("Test prefix / intermediate parsing 1",
                    "4?c",
                    (struct csi){.prefix = 0,
                                 .final = 'c',
                                 .state = CSI_ACCEPT,
                                 .n_params = 1,
                                 .params = {{.primary = 4}},
                                 .intermediate = '?'});
  test_csi_testcase(
      "Test n_params", " q", (struct csi){.prefix = ' ', .final = 'q', .state = CSI_ACCEPT, .n_params = 0});

  /* The parser incorrectly parses the intermediate byte in DECST8C as a parameter because the alternative is too annoying.
   * This is the only CSI where the workaround is required, so it is handled in the DECST8C dispatcher. */
  test_csi_testcase(
      "Test DECST8C", "?5W", (struct csi){.prefix = '?', .final = 'W', .state = CSI_ACCEPT, .n_params = 1, .params = {{ .primary = 5 }}});
}

#define assertf(cond, fmt, ...)                                                                                        \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      ERROR(fmt, __VA_ARGS__);                                                                                         \
      FAIL_ASSERT(cond);                                                                                               \
    }                                                                                                                  \
  } while (0)

void test_string(void) {
  struct string s = {0};
  string_push(&s, (uint8_t *)"Hello!");
  struct u8_slice middle = string_range(&s, 1, -2);
  struct u8_slice start = string_range(&s, 0, 3);
  struct u8_slice end = string_range(&s, 2, s.len);
  assert(middle.len == 4);
  assert(middle.content[0] == 'e');
  assert(middle.content[3] == 'o');
  assert(string_starts_with(&s, start));
  assert(string_ends_with(&s, end));
  string_shift_left(&s, 1);
  assert(s.len == 5);
  assert(string_starts_with(&s, u8_slice_from_cstr("ello!")));
  string_shift_left(&s, 2);
  assert(s.len == 3);
  assert(string_starts_with(&s, u8_slice_from_cstr("lo!")));
  string_destroy(&s);
}

static void test_base64(void) {
  struct { const char *input; const char *expected; } cases[] = {
    { "",       "" },
    { "f",      "Zg==" },
    { "fo",     "Zm8=" },
    { "foo",    "Zm9v" },
    { "foob",   "Zm9vYg==" },
    { "fooba",  "Zm9vYmE=" },
    { "foobar", "Zm9vYmFy" },
  };
  struct string out = {0};
  for (size_t i = 0; i < LENGTH(cases); i++) {
    string_clear(&out);
    struct u8_slice in = u8_slice_from_cstr(cases[i].input);
    u8_slice_encode_base64(in, &out);
    struct u8_slice result = string_as_u8_slice(out);
    if (!u8_slice_equals(result, u8_slice_from_cstr(cases[i].expected))) {
      printf("base64 failed for '%s': got '%.*s', expected '%s'\n",
             cases[i].input, (int)result.len, result.content, cases[i].expected);
      fail();
    }
  }
  string_destroy(&out);
}

static int int_less_than(const void *_a, const void *_b) {
  int a = *(int*)_a;
  int b = *(int*)_b;
  return a - b;
}


void test_vec(void) {
  int *item = NULL;
  struct vec v = vec(int);
  assert(vec_index(&v, &item) == -1);
  vec_foreach(item, v) {
    assert(!"foreach: Vec should be empty!");
  }
  vec_rforeach(item, v) {
    assert(!"rforeach: Vec should be empty!");
  }
  vec_find(item, v, *item == 1);
  assert(item == NULL);
  int push[] = {3, 6, 7, 8, 0, 1, 3, 5, 1};
  for (int i = 0; i < LENGTH(push); i++) {
    vec_push(&v, push + i);
  }
  int expected[] = {
      6, 3, 6, 7,          /* insert[0] */
      4,                   /* insert[4] #2 */
      9, 8, 0, 1, 3, 5, 1, /* insert[4] */
      7,                   /* insert[length] */
  };

  vec_insert(&v, 0, &(int){6});
  vec_insert(&v, v.length, &(int){7});
  vec_insert(&v, 4, &(int){9});
  vec_insert(&v, 4, &(int){4});
  vec_find(item, v, *item == 1);
  assert(item != NULL);
  assert(*item == 1);
  assert(vec_index(&v, item) == 8);
  vec_find(item, v, *item == 99);
  assert(item == NULL);
  ssize_t index = 0;
  vec_foreach(item, v) {
    int actual = *item;
    int exp = expected[index];
    int actual_index = vec_index(&v, item);
    assert(index == actual_index);
    assert(actual == exp);
    index++;
  }
  assert((size_t)index == v.length);
  index = v.length - 1;
  vec_rforeach(item, v) {
    int actual = *item;
    int exp = expected[index];
    int actual_index = vec_index(&v, item);
    assert(index == actual_index);
    assert(actual == exp);
    index--;
  }

  int where = 0;
  vec_where(item, v, *item > 3) where++;
  assert(where == 8);
  where = 0;
  vec_rwhere(item, v, *item < 3) where++;
  assert(where == 3);
  assert(index == -1);
  vec_rforeach(item, v) {
    vec_remove_at(&v, vec_index(&v, item));
  }
  vec_foreach(item, v) {
    assert(!"foreach: Vec should be empty!");
  }
  vec_rforeach(item, v) {
    assert(!"rforeach: Vec should be empty!");
  }
  vec_find(item, v, *item == 1);
  assert(item == NULL);
  assert(v.length == 0);

  vec_set(&v, 5, &(int){7});
  assert(v.length == 6);
  assert(*(int *)vec_nth(v, 5) == 7);
  for (int i = 0; i < 5; i++) {
    assert(*(int *)vec_nth(v, i) == 0);
  }
  vec_set(&v, 6, &(int){9});
  assert(v.length == 7);
  assert(*(int *)vec_nth(v, 5) == 7);
  assert(*(int *)vec_nth(v, 6) == 9);
  for (int i = 0; i < 5; i++) {
    assert(*(int *)vec_nth(v, i) == 0);
  }

  assert(*(int *)vec_pop(&v) == 9);
  assert(*(int *)vec_pop(&v) == 7);
  for (int i = 0; i < 5; i++) {
    assert(*(int *)vec_pop(&v) == 0);
  }
  assert(vec_pop(&v) == NULL);
  assert(vec_pop(&v) == NULL);


  #define V(x) &(int){x}
  vec_clear(&v);
  assert(vec_binsearch(v, V(0), int_less_than) == ~0);
  vec_push(&v, V(5));
  assert(vec_binsearch(v, V(4), int_less_than) == ~0);
  assert(vec_binsearch(v, V(5), int_less_than) == 0);
  assert(vec_binsearch(v, V(6), int_less_than) == ~1);
  vec_push(&v, V(10));
  assert(vec_binsearch(v, V(0), int_less_than) == ~0);
  assert(vec_binsearch(v, V(5), int_less_than) == 0);
  assert(vec_binsearch(v, V(7), int_less_than) == ~1);
  assert(vec_binsearch(v, V(9), int_less_than) == ~1);
  assert(vec_binsearch(v, V(10), int_less_than) == 1);
  assert(vec_binsearch(v, V(11), int_less_than) == ~2);
  vec_push(&v, V(15));
  assert(vec_binsearch(v, V(0), int_less_than) == ~0);
  assert(vec_binsearch(v, V(5), int_less_than) == 0);
  assert(vec_binsearch(v, V(7), int_less_than) == ~1);
  assert(vec_binsearch(v, V(9), int_less_than) == ~1);
  assert(vec_binsearch(v, V(10), int_less_than) == 1);
  assert(vec_binsearch(v, V(11), int_less_than) == ~2);
  assert(vec_binsearch(v, V(15), int_less_than) == 2);
  assert(vec_binsearch(v, V(17), int_less_than) == ~3);
  vec_push(&v, V(20));
  assert(vec_binsearch(v, V(0), int_less_than) == ~0);
  assert(vec_binsearch(v, V(5), int_less_than) == 0);
  assert(vec_binsearch(v, V(7), int_less_than) == ~1);
  assert(vec_binsearch(v, V(9), int_less_than) == ~1);
  assert(vec_binsearch(v, V(10), int_less_than) == 1);
  assert(vec_binsearch(v, V(11), int_less_than) == ~2);
  assert(vec_binsearch(v, V(15), int_less_than) == 2);
  assert(vec_binsearch(v, V(17), int_less_than) == ~3);
  assert(vec_binsearch(v, V(20), int_less_than) == 3);
  assert(vec_binsearch(v, V(21), int_less_than) == ~4);

  vec_destroy(&v);
}

static void test_string_joinpath(void) {
  struct string str = {0};
  string_joinpath(&str, "hello", "path/");
  string_ensure_null_terminated(&str);
  assert(strcmp("/hello/path/", (char *)str.content) == 0);
  string_joinpath(&str, "/addition");
  string_ensure_null_terminated(&str);
  assert(strcmp("/hello/path/addition", (char *)str.content) == 0);
  size_t here = str.len;
  string_joinpath(&str, "another one");
  string_ensure_null_terminated(&str);
  assert(strcmp("/hello/path/addition/another one", (char *)str.content) == 0);
  string_truncate(&str, here);
  string_joinpath(&str, "sike");
  string_ensure_null_terminated(&str);
  assert(strcmp("/hello/path/addition/sike", (char *)str.content) == 0);
}

static void test_num_as_slice(void) {
  uint64_t cases[] = {0, 1, 10, 1234567890, UINT64_MAX};
  char testbuf[30];
  for (int i = 0; i < LENGTH(cases); i++) {
    snprintf(testbuf, sizeof(testbuf) - 1, "%lu", cases[i]);
    struct u8_slice slice = number_as_u8_slice(cases[i]);
    assert(strcmp(testbuf, (char *)slice.content) == 0);
  }
}

static void test_lua(void);
static void test_lua_modules(void);

static void test_shmem_allocator(void) {
  size_t cap = sysconf(_SC_PAGESIZE);
  struct velvet_alloc *ally = velvet_alloc_shmem_create(cap);

  /* basic alloc returns non-null, zero-filled memory */
  char *a = ally->calloc(ally, 64, 1);
  assert(a != NULL);
  for (int i = 0; i < 64; i++) assert(a[i] == 0);

  /* multiple allocations return distinct, non-overlapping pointers */
  char *b = ally->calloc(ally, 64, 1);
  char *c = ally->calloc(ally, 64, 1);
  assert(b != NULL && c != NULL);
  assert(a != b && b != c && a != c);
  assert(abs((int)(b - a)) >= 64);
  assert(abs((int)(c - b)) >= 64);

  /* data integrity across allocations */
  memset(a, 'A', 64);
  memset(b, 'B', 64);
  memset(c, 'C', 64);
  for (int i = 0; i < 64; i++) {
    assert(a[i] == 'A');
    assert(b[i] == 'B');
    assert(c[i] == 'C');
  }

  /* free(NULL) is a no-op */
  ally->free(ally, NULL);

  /* free middle block, data in neighbors survives */
  ally->free(ally, b);
  for (int i = 0; i < 64; i++) {
    assert(a[i] == 'A');
    assert(c[i] == 'C');
  }

  /* reuse freed space */
  char *d = ally->calloc(ally, 64, 1);
  assert(d != NULL);
  for (int i = 0; i < 64; i++) assert(d[i] == 0);

  ally->free(ally, a);
  ally->free(ally, c);
  ally->free(ally, d);

  /* coalescing: after freeing everything, one big alloc should work */
  char *big = ally->calloc(ally, cap / 2, 1);
  assert(big != NULL);
  ally->free(ally, big);

  /* exhaustion: allocation larger than capacity returns NULL */
  char *too_big = ally->calloc(ally, cap, 1);
  assert(too_big == NULL);

  /* many small allocations then free all in reverse, verify coalescing recovers space */
  size_t blksz = cap / 32;
  char *ptrs[64];
  int n = 0;
  for (n = 0; n < 64; n++) {
    ptrs[n] = ally->calloc(ally, blksz, 1);
    if (!ptrs[n]) break;
    memset(ptrs[n], (char)('a' + (n % 26)), blksz);
  }
  assert(n > 0);
  for (int i = n - 1; i >= 0; i--) ally->free(ally, ptrs[i]);
  big = ally->calloc(ally, cap / 2, 1);
  assert(big != NULL);
  ally->free(ally, big);

  /* free in interleaved order to test non-adjacent coalescing */
  for (n = 0; n < 64; n++) {
    ptrs[n] = ally->calloc(ally, blksz, 1);
    if (!ptrs[n]) break;
  }
  for (int i = 0; i < n; i += 2) ally->free(ally, ptrs[i]);
  for (int i = 1; i < n; i += 2) ally->free(ally, ptrs[i]);
  big = ally->calloc(ally, cap / 2, 1);
  assert(big != NULL);
  ally->free(ally, big);

  /* non-adjacent free blocks must not coalesce: exhaust the allocator into
   * uniform blocks, free alternating ones, then request more than any single
   * gap — must fail because the free blocks are separated by live allocations */
  {
    char *e[64];
    int ne = 0;
    for (ne = 0; ne < 64; ne++) {
      e[ne] = ally->calloc(ally, blksz, 1);
      if (!e[ne]) break;
    }
    assert(ne >= 4);
    /* measure actual stride to derive a request that can't fit in one gap */
    size_t stride = (size_t)(e[0] - e[1]);
    /* free every other block */
    for (int i = 1; i < ne; i += 2) ally->free(ally, e[i]);
    /* each freed block is ~stride bytes. the leftover at the front can coalesce
     * with one adjacent freed block to form at most ~2*stride - 1 bytes.
     * requesting 2*stride guarantees it won't fit in any contiguous gap. */
    char *nope = ally->calloc(ally, 2 * stride, 1);
    assert(nope == NULL);
    /* clean up */
    for (int i = 0; i < ne; i += 2) ally->free(ally, e[i]);
  }

  /* realloc: NULL ptr acts as calloc */
  char *r1 = ally->realloc(ally, NULL, 32, 1);
  assert(r1 != NULL);
  for (int i = 0; i < 32; i++) assert(r1[i] == 0);

  /* realloc: grow copies data */
  memcpy(r1, "hello world!", 12);
  char *r2 = ally->realloc(ally, r1, 128, 1);
  assert(r2 != NULL);
  assert(memcmp(r2, "hello world!", 12) == 0);

  /* realloc: shrink preserves data */
  char *r3 = ally->realloc(ally, r2, 16, 1);
  assert(r3 != NULL);
  assert(memcmp(r3, "hello world!", 12) == 0);
  ally->free(ally, r3);

  /* nmemb * size overflow returns NULL */
  volatile size_t inval = (size_t)-1; /* hide the value from the compiler to silence diagnostic */
  char *overflow = ally->calloc(ally, inval, 2);
  assert(overflow == NULL);

  int fd = velvet_alloc_shmem_get_fd(ally);
  /* remap: data visible through second mapping */
  {
    int fd2 = dup(fd);
    assert(fd2 >= 0);
    struct velvet_alloc *ally2 = velvet_alloc_shmem_remap(fd2);
    assert(ally2 != NULL);

    char *s1 = ally->calloc(ally, 64, 1);
    assert(s1 != NULL);
    memcpy(s1, "shared!", 7);

    size_t offset = (uint8_t *)s1 - (uint8_t *)ally;
    char *s1_via_remap = (char *)((uint8_t *)ally2 + offset);
    assert(memcmp(s1_via_remap, "shared!", 7) == 0);

    velvet_alloc_shmem_destroy(ally2, fd2);
  }

  /* remap: data visible through third mapping */
  {
    int fd2 = dup(fd);
    assert(fd2 >= 0);
    struct velvet_alloc * ally3 = velvet_alloc_shmem_remap(fd2);
    assert( ally3 != NULL);

    char *s1 = ally->calloc(ally, 64, 1);
    assert(s1 != NULL);
    memcpy(s1, "shared!", 7);

    size_t offset = (uint8_t *)s1 - (uint8_t *)ally;
    char *s1_via_remap = (char *)((uint8_t *) ally3 + offset);
    assert(memcmp(s1_via_remap, "shared!", 7) == 0);

    velvet_alloc_shmem_destroy(ally3, fd2);
  }

  velvet_alloc_shmem_destroy(ally, fd);
}

int main(void) {
  test_shmem_allocator();
  test_csi_parsing();
  test_string();
  test_num_as_slice();
  test_string_joinpath();
  test_base64();
  test_vec();
  test_lua();
  test_lua_modules();
  return n_failures;
}

#include "lauxlib.h"
#include "lua.h"

static void lua_assert(lua_State *L, char *cmd) {
  if (luaL_loadstring(L, cmd) != LUA_OK) {
    lua_die(L);
  }

  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const char *err = luaL_tolstring(L, 1, 0);
    printf("%s", err);
    exit(1);
  }

  lua_pop(L, lua_gettop(L));
}

/* initialize a testing velvet struct with the bare minimum for asserts to succeed */
static struct velvet get_dumb_velvet(void) {
  struct velvet v = {
      .scene = velvet_scene_default,
      .clients = vec(struct velvet_client),
      .coroutines = vec(struct velvet_coroutine),
      .processes = vec(struct velvet_process),
      .marked_for_death = vec(struct velvet_process),
      .stored_strings = vec(struct velvet_kvp),
      .event_loop = io_default,
  };
  return v;
}

void test_lua(void) {
  struct velvet v = get_dumb_velvet();
  char *binpath = platform_get_exe_path();
  if (!binpath) velvet_die("Unable to locate library");
  char *lastslash = strrchr(binpath, '/');
  *lastslash = 0;
  /* silently ignore errors */
  if (chdir(binpath) == -1) velvet_die("chdir failed:");
  free(binpath);
  velvet_lua_init(&v);
  lua_State *L = v.L;

  char *requires[] = {
      "require('velvet')",                 /* lua/velvet/init.lua */
      "require('velvet.default_options')", /* lua/velvet/init.lua */
  };

  for (int i = 0; i < LENGTH(requires); i++) {
    if (luaL_dostring(L, requires[i]) != LUA_OK) {
      lua_die(L);
    }
    lua_pop(L, lua_gettop(L));
  }

  /* test that options are wired up correctly */
  v.scene.theme.palette[4] = (struct color){ .c.rgb.b = 0, .kind = VELVET_API_COLOR_KIND_RGB };
  lua_assert(L, "assert(vv.options.theme.blue ~= 0.0)");
  v.scene.theme.palette[4] = (struct color){ .c.rgb.b = 254, .kind = VELVET_API_COLOR_KIND_RGB };
  lua_assert(L, "assert(vv.options.theme.blue ~= 1.0)");

  velvet_destroy(&v);
}

static void set_max_fds(rlim_t n) {
  struct rlimit rlim;
  if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) velvet_die("getrlimit:");
  rlim.rlim_cur = MIN(n, rlim.rlim_max);
  if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) velvet_die("setrlimit:");
  if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) velvet_die("getrlimit:");
  assert(rlim.rlim_cur == n);
}

void test_lua_modules(void) {
  /* because the lua test context is dispatching with the real velvet event loop, if the test is terminated by a signal, (Ctrl-C, kill)
   * it will delete the $VELVET socket file in its shutdown path.
   * To avoid this, we unset the environment variable in the test context. */
  unsetenv("VELVET");

  /* in order to speed up the fd exhaustion test, we artificially cap
   * the number of open file handles. */
  set_max_fds(150);

  struct velvet v = {0};
  char *argv[] = { "-S", "test", NULL };
  int noop_fd[2];
  pipe(noop_fd);
  velvet_init(&v, noop_fd[0], "vv", argv);
  velvet_lua_init(&v);
  lua_State *L = v.L;

  if (luaL_dostring(L, "require('velvet')") != LUA_OK) {
    lua_die(L);
  }
  lua_pop(L, lua_gettop(L));

  /* lua does not have a way to set environment variables
   * in the scope of the running process, so we help it a bit here. */
  setenv("LUA_TEST_ENV", "123", true);

  lua_getglobal(L, "require");
  lua_pushstring(L, "tests");
  lua_call(L, 1, 1);
  luaL_checktype(L, -1, LUA_TTABLE);

  /* globals needed by tests.run() */
  lua_pushinteger(L, SIGTERM);
  lua_setglobal(L, "SIGTERM");

  /* if stdout/err are tty's, the harness can print colors */
  lua_pushboolean(L, isatty(STDOUT_FILENO));
  lua_setglobal(L, "STDOUT_ISATTY");
  lua_pushboolean(L, isatty(STDERR_FILENO));
  lua_setglobal(L, "STDERR_ISATTY");

  /* start tests and dispatch the main loop until TEST_STATUS is set */
  lua_getfield(L, -1, "run");
  if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
    const char *err = lua_tostring(L, -1);
    velvet_die("tests.run(): %s", err);
  }

  bool success = false;
  while (true) {
    lua_getglobal(L, "TEST_STATUS");
    if (!lua_isnoneornil(L, -1)) {
      success = lua_toboolean(L, -1);
      break;
    }
    lua_pop(L, 1);
    /* io_dispatch will wait for and invoke any pending schedules */
    velvet_dispatch(&v);
  }
  struct velvet_process *p;
  vec_where(p, v.processes, p->pid) kill(p->pid, SIGKILL);
  vec_where(p, v.marked_for_death, p->pid) kill(p->pid, SIGKILL);
  
  /* velvet_destroy will kill any processes the tests failed to dispose. */
  velvet_destroy(&v);
  /* no need for asserts, the lua test suite already reported nice errors */
  if (!success) exit(1);
}
