#include "osc.h"
#include "utils.h"

static bool osc_dispatch_todo(struct fsm *fsm, struct osc *osc) {
  (void)fsm;
  TODO("OSC %.*s", osc->len, osc->buffer);
  return false;
}

bool osc_dispatch(struct fsm *fsm, struct osc *osc) {
  assert(osc->state == OSC_ACCEPT);
  return osc_dispatch_todo(fsm, osc);
}

// OSC sequences follow the syntax:
// OSC Ps ; Pt ; BEL -- OR
// OSC Ps ; Pt ; ST
// The terminator does not affect semantics of the command,
// but should be stored for queries so the response can use the same terminator used by the query as this is likely what
// the client expects.
int osc_parse(struct osc *o, const uint8_t *buffer, int len) {
  o->buffer = buffer;
  o->len = len;
  o->state = OSC_ACCEPT;
  return 0;
}
