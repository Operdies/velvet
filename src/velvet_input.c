#include "velvet_input.h"
#include "utils.h"

#define ESC 0x1b
#define CSI '['
#define BRACKETED_PASTE_MAX (1 << 20)
#define CSI_BUFFER_MAX (256)

static const struct u8_slice bracketed_paste_start = STRING_SLICE(u8"\x1b[200~");
static const struct u8_slice bracketed_paste_end = STRING_SLICE(u8"\x1b[201~");

static void normal(struct velvet_input *in, uint8_t ch) {
  if (ch == in->prefix) {
    in->state = VELVET_INPUT_STATE_PREFIX;
    return;
  }
  switch (ch) {
    case ESC: in->state = VELVET_INPUT_STATE_ESC; break;
    default: break;
  }
}

static void csi(struct velvet_input *in, uint8_t ch) {
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

  switch (ch) {
    default: break;
  }
}

static void esc(struct velvet_input *in, uint8_t ch) {
  string_push_char(&in->command_buffer, ch);
  switch (ch) {
    case CSI: in->state = VELVET_INPUT_STATE_CSI; break;
    default: break;
  }
}


static void prefix(struct velvet_input *in, uint8_t ch) {
  in->state = VELVET_INPUT_STATE_NORMAL;
  switch (ch) {
    default: break;
  }
}

static void bracketed_paste(struct velvet_input *in, uint8_t ch) {
  string_push_char(&in->command_buffer, ch);

  if (in->command_buffer.len > BRACKETED_PASTE_MAX) {
    ERROR("Bracketed Paste max exceeded!!");
    string_clear(&in->command_buffer);
    in->state = VELVET_INPUT_STATE_NORMAL;
    return;
  }

  switch (ch) {
    case CSI: in->state = CSI; break;
    default: break;
  }
}

void velvet_input_process(struct velvet_input *in, struct u8_slice str) {
  for (size_t i = 0; i < str.len; i++) {
    uint8_t ch = str.content[i];
    switch ((enum velvet_input_state)in->state) {
    case VELVET_INPUT_STATE_NORMAL: normal(in, ch); break;
    case VELVET_INPUT_STATE_ESC: esc(in, ch); break;
    case VELVET_INPUT_STATE_CSI: csi(in, ch);  break;
    case VELVET_INPUT_STATE_PREFIX:
    case VELVET_INPUT_STATE_PREFIX_CONT: break;
    case VELVET_INPUT_STATE_BRACKETED_PASTE: bracketed_paste(in, ch); break;
    }
  }
  multiplexer_feed_input(in->m, str);
}
