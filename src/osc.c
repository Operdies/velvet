#include "osc.h"
#include "utils.h"

#include <ctype.h>
#include <string.h>
#include "vte_host.h"

static bool osc_dispatch_todo(struct vte *vte, struct osc *osc) {
  (void)vte;
  TODO("OSC %d %.*s", osc->ps, osc->pt.len, osc->pt.text);
  return false;
}

static bool osc_dispatch_foreground_color(struct vte *vte, struct osc *osc) {
  if (osc->pt.len && osc->pt.text[0] == '?') {
    string_push(&vte->pending_output, u8"\x1b]10;rgb:ffff/ffff/ffff");
    string_push(&vte->pending_output, osc->st);
    return true;
  }
  return osc_dispatch_todo(vte, osc);
}

static bool osc_dispatch_background_color(struct vte *vte, struct osc *osc) {
  if (osc->pt.len && osc->pt.text[0] == '?') {
    string_push(&vte->pending_output, u8"\x1b]11;rgb:0000/0000/0000");
    string_push(&vte->pending_output, osc->st);
    return true;
  }
  return osc_dispatch_todo(vte, osc);
}

// used by lazygit
// would be great to support well
static bool osc_dispatch_hyperlink(struct vte *vte, struct osc *osc) {
  (void)vte;
  TODO("OSC hyperlink %d %.*s", osc->ps, osc->pt.len, osc->pt.text);
  return false;
}

// TODO: This kinda sucks
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)((char *)__mptr - offsetof(type,member));})

static bool osc_set_title(struct vte *vte, struct osc *osc) {
  struct vte_host *container;
  container = container_of(vte, struct vte_host, vte);
  container->border_dirty = true;
  strncpy(container->title, (char*)osc->pt.text, MIN(osc->pt.len, (int)sizeof(container->title) - 1));
  return true;
}
static bool osc_set_icon(struct vte *vte, struct osc *osc) {
  struct vte_host *container;
  container = container_of(vte, struct vte_host, vte);
  container->border_dirty = true;
  strncpy(container->icon, (char*)osc->pt.text, MIN(osc->pt.len, (int)sizeof(container->icon) - 1));
  return true;
}

bool osc_dispatch(struct vte *vte, struct osc *osc) {
  assert(osc->state == OSC_ACCEPT);
  switch (osc->ps) {
  case OSC_SET_ICON_AND_TITLE: return osc_set_title(vte, osc) && osc_set_icon(vte, osc);
  case OSC_SET_ICON: return osc_set_icon(vte, osc);
  case OSC_SET_TITLE: return osc_set_title(vte, osc);
  case OSC_HYPERLINK: return osc_dispatch_hyperlink(vte, osc);
  case OSC_FOREGROUND_COLOR: return osc_dispatch_foreground_color(vte, osc);
  case OSC_BACKGROUND_COLOR: return osc_dispatch_background_color(vte, osc);
  default: return osc_dispatch_todo(vte, osc);
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
int osc_parse(struct osc *o, struct u8_slice str, const uint8_t *st) {
  int ps = 0;
  o->st = st;

  size_t i = 0;
  for (; i < str.len && isdigit(str.content[i]); i++) {
    ps *= 10;
    ps += str.content[i] - '0';
  }

  o->ps = ps;

  if (ps == 8) {
    // OSC 8 can have optional parameters and thus use ane extra semicolon.
    i += osc_parse_parameters(o, str.content + i, str.len - i);
    if (o->state == OSC_REJECT) {
      return i;
    }
  }

  if (str.content[i] != ';') {
    // These particular OSC commands to not require a Pt argument
    if (ps == 110 || ps == 111 || ps == 112) {
      o->state = OSC_ACCEPT;
      return i;
    }
    logmsg("Reject OSC: Expected semicolon before Pt");
    o->state = OSC_REJECT;
    return i;
  }
  i++;
  const uint8_t *pt = &str.content[i];
  o->state = OSC_ACCEPT;
  o->pt = (struct osc_pt){.len = str.len - i, .text = pt};

  return i;
}
