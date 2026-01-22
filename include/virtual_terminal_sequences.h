#ifndef VIRTUAL_TERMINAL_SEQUENCES_H

#include <stdint.h>
#include "collections.h"

#define ESC "\x1b"
#define CSI ESC "["

#define VT(name, command) static const struct u8_slice vt_##name = {.content = command, .len = sizeof(command) - 1}

#define VT_ANSI_MODE(name, mode)                                                                                            \
  static const struct u8_slice vt_##name##_on = {.content = CSI #mode "h", .len = sizeof(CSI "?" #mode "h") - 1};    \
  static const struct u8_slice vt_##name##_off = {.content = CSI #mode "l", .len = sizeof(CSI "?" #mode "l") - 1};   \
  static const struct u8_slice vt_##name##_query = {.content = CSI #mode "$p", .len = sizeof(CSI "?" #mode "$p") - 1};

#define VT_PRIVATE_MODE(name, mode)                                                                                            \
  static const struct u8_slice vt_##name##_on = {.content = CSI "?" #mode "h", .len = sizeof(CSI "?" #mode "h") - 1};    \
  static const struct u8_slice vt_##name##_off = {.content = CSI "?" #mode "l", .len = sizeof(CSI "?" #mode "l") - 1};   \
  static const struct u8_slice vt_##name##_query = {.content = CSI "?" #mode "$p", .len = sizeof(CSI "?" #mode "$p") - 1};

VT(leave_alternate_screen, CSI "2J" CSI "H" CSI "?1049l");
VT(enter_alternate_screen, CSI "?1049h" CSI "2J" CSI "H");
VT(focus_out, CSI "O");
VT(focus_in, CSI "I");
VT(clear, CSI "2J");
VT(kitty_keyboard_on, CSI ">31u");
VT(kitty_keyboard_off, CSI "<u");

VT_ANSI_MODE(application_mode, 1);
VT_PRIVATE_MODE(synchronized_rendering, 2026);
VT_PRIVATE_MODE(mouse_mode_sgr, 1006);
VT_PRIVATE_MODE(mouse_mode_sgr_pixel, 1016);
VT_PRIVATE_MODE(mouse_tracking, 1003);
VT_PRIVATE_MODE(line_wrapping, 7);
VT_PRIVATE_MODE(cursor_visible, 25);
VT_PRIVATE_MODE(focus_reporting, 1004);
VT_PRIVATE_MODE(bracketed_paste, 2004);

#undef VT
#undef ESC
#undef CSI

#endif // VIRTUAL_TERMINAL_SEQUENCES_H
