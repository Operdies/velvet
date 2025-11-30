#ifndef VIRTUAL_TERMINAL_SEQUENCES_H

#include <stdint.h>
#include "collections.h"

#define ESC u8"\x1b"
#define CSI ESC "["

#define VT(name, command) static const struct u8_slice vt_##name = {.content = command, .len = sizeof(command) - 1}

#define VT_MODE(name, mode)                                                                                            \
  static const struct u8_slice vt_##name##_on = {.content = CSI "?" #mode "h", .len = sizeof(CSI "?" #mode "h") - 1};    \
  static const struct u8_slice vt_##name##_off = {.content = CSI "?" #mode "l", .len = sizeof(CSI "?" #mode "l") - 1};   \
  static const struct u8_slice vt_##name##_query = {.content = CSI "?" #mode "$p", .len = sizeof(CSI "?" #mode "$p") - 1};

VT(leave_alternate_screen, CSI "2J" CSI "H" CSI "?1049l");
VT(enter_alternate_screen, CSI "?1049h" CSI "2J" CSI "H");
VT(focus_out, CSI "O");
VT(focus_in, CSI "I");
VT(clear, CSI "2J");

VT_MODE(synchronized_rendering, 2026);
VT_MODE(mouse_mode_sgr, 1006);
VT_MODE(mouse_mode_sgr_pixel, 1016);
VT_MODE(mouse_tracking, 1003);
VT_MODE(line_wrapping, 7);
VT_MODE(cursor_visible, 7);
VT_MODE(focus_reporting, 1004);
VT_MODE(bracketed_paste, 2004);

#undef VT
#undef ESC
#undef CSI

#endif // VIRTUAL_TERMINAL_SEQUENCES_H
