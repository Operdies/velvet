#include "osc.h"
#include "queries.h"
#include "utils.h"

#include <ctype.h>
#include <string.h>

static bool osc_dispatch_todo(struct fsm *fsm, struct osc *osc) {
  (void)fsm;
  TODO("OSC %d %.*s", osc->ps, osc->pt.len, osc->pt.text);
  return false;
}

static bool osc_dispatch_background_color(struct fsm *fsm, struct osc *osc) {
  if (osc->pt.len == 1 && osc->pt.text[0] == '?') {
    struct emulator_query q = {.type = REQUEST_DEFAULT_BG};
    memcpy(q.st, osc->st, sizeof(osc->st));
    vec_push(&fsm->pending_requests, &q);
    return true;
  }
  return osc_dispatch_todo(fsm, osc);
}
static bool osc_dispatch_hyperlink(struct fsm *fsm, struct osc *osc) {
  return osc_dispatch_todo(fsm, osc);
}

bool osc_dispatch(struct fsm *fsm, struct osc *osc) {
  assert(osc->state == OSC_ACCEPT);
  switch (osc->ps) {
  case OSC_HYPERLINK: return osc_dispatch_hyperlink(fsm, osc);
  case OSC_BACKGROUND_COLOR: return osc_dispatch_background_color(fsm, osc);
  default: return osc_dispatch_todo(fsm, osc);
  };
}

static int osc_parse_parameters(struct osc *o, const uint8_t *buffer, int len) {
  int i = 0;
  if (buffer[i] != ';') {
    logmsg("Reject OSC: Expected semicolon after Ps");
    o->state = OSC_REJECT;
    return i;
  }
  i++;

  // empty parameter list
  if (buffer[i] == ';') {
    o->state = OSC_ACCEPT;
    return i;
  }

  while (i < len && o->n_parameters < OSC_MAX_PARAMS) {
    struct osc_pt key = {.text = &buffer[i]};
    // Parameters are of the form `key1=value1:key2=value2`
    for (; i < len && buffer[i] != '='; i++);
    if (buffer[i] != '=') {
      o->state = OSC_REJECT;
      logmsg("Reject OSC: Expected '=' in parameter.");
      return i;
    }
    key.len = &buffer[i] - key.text;
    i++;
    struct osc_pt value = {.text = &buffer[i]};
    for (; i < len && buffer[i] != ':' && buffer[i] != ';'; i++);
    if (buffer[i] != ':' && buffer[i] != ';') {
      o->state = OSC_REJECT;
      logmsg("Reject OSC: Expected parameter value to be terminated by ';' or ':'.");
      return i;
    }
    value.len = &buffer[i] - value.text;
    o->parameters[o->n_parameters] = (struct osc_parameter){.value = value, .key = key};
    o->n_parameters++;
    if (buffer[i] == ';') {
      o->state = OSC_ACCEPT;
      return i;
    }
    i++;
  }
  return 0;
}

// OSC sequences follow the syntax:
// OSC Ps ; Pt BEL -- OR
// OSC Ps ; Pt ST
// The terminator does not affect semantics of the command,
// but should be stored for queries so the response can use the same terminator used by the query as this is likely what
// the client expects.
int osc_parse(struct osc *o, const uint8_t *buffer, int len, const uint8_t *st) {
  int ps = 0;
  {
    int n_st = 0;
    for (; st[n_st]; n_st++) o->st[n_st] = st[n_st];
  }

  int i = 0;
  for (; i < len && isdigit(buffer[i]); i++) {
    ps *= 10;
    ps += buffer[i] - '0';
  }

  o->ps = ps;

  if (ps == 8) {
    // OSC 8 can have optional parameters and thus use ane extra semicolon.
    i += osc_parse_parameters(o, buffer + i, len - i);
    if (o->state == OSC_REJECT) {
      return i;
    }
  }

  if (buffer[i] != ';') {
    logmsg("Reject OSC: Expected semicolon before Pt");
    o->state = OSC_REJECT;
    return i;
  }
  i++;
  const uint8_t *pt = &buffer[i];
  o->state = OSC_ACCEPT;
  o->pt = (struct osc_pt){.len = len - i, .text = pt};

  return 0;
}
