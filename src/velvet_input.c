#include "velvet_input.h"
#include "utils.h"

#define ESC 0x1b
#define CSI '['
#define BRACKETED_PASTE_MAX (1 << 20)
#define CSI_BUFFER_MAX (256)

static const struct u8_slice bracketed_paste_start = STRING_SLICE(u8"\x1b[200~");
static const struct u8_slice bracketed_paste_end = STRING_SLICE(u8"\x1b[201~");

static void send(struct velvet_input *in, struct u8_slice s) {
  struct vte_host *focus = vec_nth(&in->m->hosts, in->m->focus);
  string_push_slice(&focus->vte.pending_input, s);
}
static void send_byte(struct velvet_input *in, uint8_t ch) {
  struct vte_host *focus = vec_nth(&in->m->hosts, in->m->focus);
  string_push_char(&focus->vte.pending_input, ch);
}

static void dispatch_normal(struct velvet_input *in, uint8_t ch) {
  assert(in->command_buffer.len == 0);
  if (ch == in->prefix) {
    in->state = VELVET_INPUT_STATE_PREFIX;
    return;
  }
  switch (ch) {
  case ESC: {
    string_push_char(&in->command_buffer, ch);
    in->state = VELVET_INPUT_STATE_ESC;
  } break;
  default: send_byte(in, ch); break;
  }
}

static void send_csi(struct velvet_input *in) {
  TODO("Input CSI: %.*s", in->command_buffer.len, in->command_buffer.content);
  string_clear(&in->command_buffer);
}

static void dispatch_csi(struct velvet_input *in, uint8_t ch) {
  string_push_char(&in->command_buffer, ch);
  if (in->command_buffer.len > CSI_BUFFER_MAX) {
    ERROR("CSI max exceeded!!");
    string_clear(&in->command_buffer);
    in->state = VELVET_INPUT_STATE_NORMAL;
    return;
  }

  if (in->command_buffer.len == bracketed_paste_start.len &&
      string_starts_with(&in->command_buffer, bracketed_paste_start)) {
    in->state = VELVET_INPUT_STATE_BRACKETED_PASTE;
    return;
  }

  // TODO: Is this accurate?
  if (ch >= 0x40 && ch <= 0x7E) {
    send_csi(in);
    in->state = VELVET_INPUT_STATE_NORMAL;
  }
}

static void dispatch_esc(struct velvet_input *in, uint8_t ch) {
  switch (ch) {
  case CSI: {
    string_push_char(&in->command_buffer, ch);
    in->state = VELVET_INPUT_STATE_CSI;
  } break;
  default: {
    send_byte(in, ESC);
    send_byte(in, ch);
  } break;
  }
}

static void dispatch_prefix(struct velvet_input *in, uint8_t ch) {
  in->state = VELVET_INPUT_STATE_NORMAL;
  switch (ch) {
  default: break;
  }
}

static void send_bracketed_paste(struct velvet_input *in) {
  struct vte_host *focus = vec_nth(&in->m->hosts, in->m->focus);
  bool enclose = focus->vte.options.bracketed_paste;
  uint8_t *start = in->command_buffer.content;
  size_t len = in->command_buffer.len;
  if (!enclose) {
    start += bracketed_paste_start.len;
    len -= bracketed_paste_start.len + bracketed_paste_end.len;
  }
  struct u8_slice s = {.content = start, .len = len};
  string_push_slice(&focus->vte.pending_input, s);
  string_clear(&in->command_buffer);
}

static void dispatch_bracketed_paste(struct velvet_input *in, uint8_t ch) {
  string_push_char(&in->command_buffer, ch);

  if (in->command_buffer.len > BRACKETED_PASTE_MAX) {
    ERROR("Bracketed Paste max exceeded!!");
    string_clear(&in->command_buffer);
    in->state = VELVET_INPUT_STATE_NORMAL;
    return;
  }

  if (string_ends_with(&in->command_buffer, bracketed_paste_end)) {
    send_bracketed_paste(in);
    in->state = VELVET_INPUT_STATE_NORMAL;
  }
}

void velvet_input_process(struct velvet_input *in, struct u8_slice str) {
  for (size_t i = 0; i < str.len; i++) {
    uint8_t ch = str.content[i];
    switch (in->state) {
    case VELVET_INPUT_STATE_NORMAL: dispatch_normal(in, ch); break;
    case VELVET_INPUT_STATE_ESC: dispatch_esc(in, ch); break;
    case VELVET_INPUT_STATE_CSI: dispatch_csi(in, ch); break;
    case VELVET_INPUT_STATE_PREFIX:
    case VELVET_INPUT_STATE_PREFIX_CONT: dispatch_prefix(in, ch); break;
    case VELVET_INPUT_STATE_BRACKETED_PASTE: dispatch_bracketed_paste(in, ch); break;
    }
  }
}
