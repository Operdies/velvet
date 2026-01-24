#include "osc.h"
#include "utils.h"

#include <ctype.h>
#include <string.h>
#include "vte.h"

static bool osc_dispatch_todo(struct vte *vte, struct osc *osc) {
  (void)vte;
  TODO("OSC %d %.*s", osc->ps, (int)osc->pt.len, osc->pt.content);
  return false;
}

static bool osc_dispatch_foreground_color(struct vte *vte, struct osc *osc) {
  if (osc->pt.len && osc->pt.content[0] == '?') {
    string_push(&vte->pending_input, (uint8_t*)"\x1b]10;rgb:ffff/ffff/ffff");
    string_push(&vte->pending_input, osc->st);
    return true;
  }
  return osc_dispatch_todo(vte, osc);
}

static bool osc_dispatch_background_color(struct vte *vte, struct osc *osc) {
  if (osc->pt.len && osc->pt.content[0] == '?') {
    string_push(&vte->pending_input, (uint8_t*)"\x1b]11;rgb:0000/0000/0000");
    string_push(&vte->pending_input, osc->st);
    return true;
  }
  return osc_dispatch_todo(vte, osc);
}

/* arbitrary enormous start value */
static uint64_t hyperlink_sequence = 446744073709551615;
static uint64_t osc_hyperlink_new_id(void) {
  return ++hyperlink_sequence;
}

static bool osc_get_id(struct osc *osc, struct u8_slice *id) {
  for (int i = 0; i < osc->n_parameters; i++) {
    if (u8_match(osc->parameters[i].key, "id")) {
      *id = osc->parameters[i].value;
      return true;
    }
  }
  return false;
}

hyperlink_handle osc_get_hyperlink_handle(struct osc_hyperlink *link) {
  return link;
}

static struct u8_slice hyperlink_get_raw_id(struct vte *vte, struct osc_hyperlink *link) {
  struct string pre = vte->osc.link_id_prefix;
  return u8_slice_range(hyperlink_get_id(link), pre.len, -1);
}

/* neovim uses hyperlinks in the built-in manual */
static bool osc_dispatch_hyperlink(struct vte *vte, struct osc *osc) {
  (void)vte;
  if (osc->pt.len == 0) {
    vte->current_link = NULL;
    return true;
  }

  struct osc_hyperlink **linkptr = NULL;
  struct u8_slice url = osc->pt;
  struct u8_slice id = {0};
  if (osc_get_id(osc, &id)) {
    /* we can reuse the same hyperlink object if the id is equal,
    * and the url is equal. Otherwise we must create a new hyperlink. */
    vec_find(linkptr, vte->links, u8_slice_equals(id, hyperlink_get_raw_id(vte, *linkptr)));
    if (linkptr && u8_slice_equals(hyperlink_get_url(*linkptr), url)) {
      vte->current_link = *linkptr;
      return true;
    }
    /* id exists, but the url does not match. We should generate a new id. */
    if (linkptr) id = (struct u8_slice){0};
  }

  linkptr = vec_new_element(&vte->links);
  struct osc_hyperlink *link = velvet_calloc(1, sizeof(*link));
  *linkptr = link;
  string_push_string(&link->buffer, vte->osc.link_id_prefix);
  if (id.content && id.len) {
    string_push_slice(&link->buffer, id);
  } else {
    uint64_t synthetic_id = osc_hyperlink_new_id();
    while (synthetic_id) {
      uint8_t digit = synthetic_id % 10;
      synthetic_id /= 10;
      string_push_char(&link->buffer, '0' + digit);
    }
  }
  link->id_len = link->buffer.len;
  string_push_slice(&link->buffer, url);
  vte->current_link = link;

  return true;
}

static bool osc_set_title(struct vte *vte, struct osc *osc) {
  string_clear(&vte->osc.title);
  struct u8_slice new_title = { .content = osc->pt.content, .len = osc->pt.len };
  string_push_slice(&vte->osc.title, new_title);
  return true;
}

static bool osc_set_icon(struct vte *vte, struct osc *osc) {
  string_clear(&vte->osc.icon);
  struct u8_slice new_title = { .content = osc->pt.content, .len = osc->pt.len };
  string_push_slice(&vte->osc.icon, new_title);
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
    velvet_log("Reject OSC: Expected semicolon after Ps");
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
    struct u8_slice key = {.content = &buffer[i]};
    // Parameters are of the form `key1=value1:key2=value2`
    for (; i < len && buffer[i] != '='; i++);
    if (buffer[i] != '=') {
      o->state = OSC_REJECT;
      velvet_log("Reject OSC: Expected '=' in parameter.");
      return i;
    }
    key.len = &buffer[i] - key.content;
    i++;
    struct u8_slice value = {.content = &buffer[i]};
    for (; i < len && buffer[i] != ':' && buffer[i] != ';'; i++);
    if (buffer[i] != ':' && buffer[i] != ';') {
      o->state = OSC_REJECT;
      velvet_log("Reject OSC: Expected parameter value to be terminated by ';' or ':'.");
      return i;
    }
    value.len = &buffer[i] - value.content;
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
    velvet_log("Reject OSC: Expected semicolon before Pt");
    o->state = OSC_REJECT;
    return i;
  }
  i++;
  const uint8_t *pt = &str.content[i];
  o->state = OSC_ACCEPT;
  o->pt = (struct u8_slice){.len = str.len - i, .content = pt};

  return i;
}

struct u8_slice hyperlink_get_id(struct osc_hyperlink *link) {
  return string_range(&link->buffer, 0, link->id_len);
}

struct u8_slice hyperlink_get_url(struct osc_hyperlink *link) {
  return string_range(&link->buffer, link->id_len, -1);
}

void hyperlink_destroy(struct osc_hyperlink *link) {
  string_destroy(&link->buffer);
  link->id_len = 0;
}
