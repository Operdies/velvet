#ifndef OSC_H
#define OSC_H

#include <stdint.h>
#include <unistd.h>
#include "collections.h"

#define OSC_MAX_PARAMS 16

enum osc_vte_state {
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

struct osc_parameter {
  struct u8_slice key;
  struct u8_slice value;
};

struct osc_hyperlink {
  struct string buffer;
  int id_len;  /* the id and url can be inferred from this */
};
typedef struct osc_hyperlink* hyperlink_handle;

struct osc {
  enum osc_vte_state state;
  enum osc_command ps;
  struct u8_slice pt;
  const uint8_t *st; // either BEL (\a) or ST (\x1b\)
  struct osc_parameter parameters[OSC_MAX_PARAMS];
  int n_parameters;
};

struct vte;
int osc_parse(struct osc *c, struct u8_slice str, const uint8_t *st);
bool osc_dispatch(struct vte *vte, struct osc *osc);
hyperlink_handle osc_get_hyperlink_handle(struct osc_hyperlink *link);
struct u8_slice hyperlink_get_id(struct osc_hyperlink *link);
struct u8_slice hyperlink_get_url(struct osc_hyperlink *link);
void hyperlink_destroy(struct osc_hyperlink *link);

#endif /*  OSC_H */
