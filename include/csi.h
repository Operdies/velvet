#ifndef CSI_H
#define CSI_H

#include <stdint.h>
#include "emulator.h"
#define CSI_MAX_PARAMS 16

enum csi_fsm_state {
  CSI_GROUND,
  CSI_ACCEPT,
  CSI_PARAMETER,
  CSI_LEADING,
  CSI_INTERMEDIATE,
  CSI_REJECT,
};

struct csi_param {
  uint32_t primary;
  uint8_t sub[4];
};

struct csi {
  enum csi_fsm_state state;
  struct {
    struct csi_param params[CSI_MAX_PARAMS];
    int n_params;
    uint8_t leading;
    uint8_t intermediate;
    uint8_t final;
  };
};

int csi_parse_parameters(struct csi *c, const uint8_t *buffer, int len);
bool csi_dispatch(struct fsm *fsm, struct csi *csi);

#endif /*  CSI_H */
