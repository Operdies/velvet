#ifndef OSC_H
#define OSC_H

#include "emulator.h"
#include <stdint.h>
#include <unistd.h>

#define OSC_MAX_PARAMS 16

enum osc_fsm_state {
  OSC_GROUND,
  OSC_ACCEPT,
  OSC_REJECT,
};

enum osc_command {
  OSC_SET_ICON_AND_TITLE = 0,
  OSC_SET_ICON = 1,
  OSC_SET_TITLE = 2,
  OSC_HYPERLINK = 8,
  OSC_FOREGROUND_COLOR = 10,
  OSC_BACKGROUND_COLOR = 11,
  OSC_CURSOR_COLOR = 12,
};

struct osc_pt {
  const uint8_t *text;
  int len;
};

struct osc_parameter {
  struct osc_pt key;
  struct osc_pt value;
};

struct osc {
  enum osc_fsm_state state;
  enum osc_command ps;
  struct osc_pt pt;
  uint8_t st[2];
  struct osc_parameter parameters[OSC_MAX_PARAMS];
  int n_parameters;
};

int osc_parse(struct osc *c, const uint8_t *buffer, int len, const uint8_t *st);
bool osc_dispatch(struct fsm *fsm, struct osc *osc);

#endif /*  OSC_H */
