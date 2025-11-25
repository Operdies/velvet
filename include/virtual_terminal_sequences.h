#ifndef VIRTUAL_TERMINAL_SEQUENCES_H

#include <stdint.h>
#include "collections.h"

#define ESC u8"\x1b"
#define CSI ESC "["

#define VT(name, command) static const struct string_slice vt_##name = { .content = command, .len = sizeof(command) - 1 }

VT(hide_cursor, CSI "?25l");
VT(show_cursor, CSI "?25h");
VT(leave_alternate_screen, CSI "2J" CSI "H" CSI "?1049l");
VT(enter_alternate_screen, CSI "?1049h" CSI "2J" CSI "H");
VT(disable_line_wrapping, CSI "?7l");
VT(enable_line_wrapping, CSI "?7h");
VT(focus_out, CSI "O");
VT(focus_in, CSI "I");
VT(clear, CSI "2J");
VT(synchronized_rendering_on, CSI "?2026h");
VT(synchronized_rendering_off, CSI "?2026l");

#undef VT
#undef ESC
#undef CSI

#endif // VIRTUAL_TERMINAL_SEQUENCES_H
