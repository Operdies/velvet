#ifndef CSI_H
#define CSI_H

#include "emulator.h"

void csi_parse_and_execute_buffer(struct fsm *fsm, const uint8_t *buffer, int n);

#endif /*  CSI_H */
