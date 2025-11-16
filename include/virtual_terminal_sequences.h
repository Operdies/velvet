#ifndef VIRTUAL_TERMINAL_SEQUENCES_H

#include <stdint.h>

#define ESC u8"\x1b"
#define CSI ESC "["

static const uint8_t vt_hide_cursor[] = CSI "?25l";
static const uint8_t vt_show_cursor[] = CSI "?25h";
static const uint8_t vt_leave_alternate_screen[] = CSI "2J" CSI "H" CSI "?1049l";
static const uint8_t vt_enter_alternate_screen[] = CSI "?1049h" CSI "2J" CSI "H";
static const uint8_t vt_disable_line_wrapping[] = CSI "?7l";
static const uint8_t vt_enable_line_wrapping[] = CSI "?7h";
static const uint8_t vt_focus_out[] = CSI "O";
static const uint8_t vt_focus_in[] = CSI "I";

#undef ESC
#undef CSI

#endif // VIRTUAL_TERMINAL_SEQUENCES_H
