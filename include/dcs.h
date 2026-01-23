#ifndef DCS_H
#define DCS_H

#include "vte.h"

void dcs_dispatch(struct vte *vte, struct u8_slice cmd, char *st);

#endif /* DCS_H */
