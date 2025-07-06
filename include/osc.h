#ifndef OSC_H
#define OSC_H

#include "emulator.h"
#include <stdint.h>
#define OSC_MAX_PARAMS 16

enum osc_fsm_state {
  OSC_GROUND,
  OSC_ACCEPT,
  OSC_PARAMETER,
  OSC_LEADING,
  OSC_INTERMEDIATE,
  OSC_REJECT,
};

enum osc_command {
  OSC_HYPERLINK_BEGIN,
  OSC_HYPERLINK_END,
};

struct osc_param {
  uint32_t primary;
  uint8_t sub[4];
};

struct osc {
  enum osc_fsm_state state;
  const uint8_t *buffer;
  int len;
};

int osc_parse(struct osc *c, const uint8_t *buffer, int len);
bool osc_dispatch(struct fsm *fsm, struct osc *osc);

#endif /*  OSC_H */
