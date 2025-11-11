#include "csi.h"
#include "utils.h"
#include <ctype.h>

#define PARAMETER(X) (isdigit((X)) || (X) == ';')
#define INTERMEDIATE(X) (((X) >= 0x20 && (X) <= 0x2F) || ((X) >= 0x3C && (X) <= 0x3F) || ((X) >= 0x5E && (X) <= 0x60))
#define ACCEPT(X) ((X) >= 0x40 && (X) <= 0x7E)

/** State machine:
 * Ground       --> Parameter
 *              --> Leading
 *              --> Accept
 * Parameter    --> Parameter
 *              --> Intermediate
 *              --> Accept
 * Leading      --> Parameter
 *              --> Intermediate
 *              --> Accept
 * Intermediate --> Accept
 */

static bool csi_read_subparameter(const uint8_t *buffer, uint8_t separator, int *value, int *read) {
  int i = 0;
  if (buffer[0] == separator) {
    i++;
    int v = 0;
    while (isdigit(buffer[i])) {
      v *= 10;
      v += buffer[i] - '0';
      i++;
    }
    *value = v;
    *read = i;
    return true;
  }
  return false;
}

static bool csi_read_parameter(struct csi_param *param, const uint8_t *buffer, int *read, bool is_sgr) {
  int i = 0;
  if (buffer[i] == ';') i++;
  int num = 0;
  while (isdigit(buffer[i])) {
    num *= 10;
    num += buffer[i] - '0';
    i++;
  }
  param->primary = num;

  // Only parse subparameters in SGR sequences
  if (is_sgr) {
    int n_subparameters = 0;
    int value, length, color_type;
    value = length = color_type = 0;
    bool is_custom_color = is_sgr && (num == 38 || num == 48);
    uint8_t separator = ':';
    int subparameter_max = LENGTH(param->sub);
    if (is_custom_color && buffer[i] == ';') {
      separator = ';';
      bool did_read = csi_read_subparameter(buffer + i, separator, &color_type, &length);
      i += length;
      if (!did_read || (color_type != 2 && color_type != 5)) {
        logmsg("Reject SGR %d: Missing color parameter", num);
        *read = i;
        return false;
      }
      param->sub[0] = color_type;
      n_subparameters++;
      subparameter_max = color_type == 2 ? 4 : 2;
    }

    while (n_subparameters < subparameter_max && csi_read_subparameter(buffer + i, separator, &value, &length)) {
      i += length;
      param->sub[n_subparameters] = value;
      n_subparameters++;
    }
    if (csi_read_subparameter(buffer + i, ':', &value, &length)) {
      logmsg("Reject CSI: Too many subparameters");
      *read = i;
      return false;
    }
  }

  *read = i;
  return true;
}

int csi_parse(struct csi *c, const uint8_t *buffer, int len) {
  if (len < 1) {
    c->state = CSI_REJECT;
    return 0;
  }
  bool is_sgr = buffer[len - 1] == 'm';
  int i = 0;
  for (; i < len;) {
    char ch = buffer[i];
    switch (c->state) {
    case CSI_GROUND: {
      c->state = PARAMETER(ch) ? CSI_PARAMETER : INTERMEDIATE(ch) ? CSI_LEADING : ACCEPT(ch) ? CSI_ACCEPT : CSI_REJECT;
    } break;
    case CSI_PARAMETER: {
      if (c->n_params >= CSI_MAX_PARAMS) {
        c->state = CSI_REJECT;
        logmsg("Reject CSI: Too many numeric parameters");
        return i;
      }

      struct csi_param *param = &c->params[c->n_params];
      c->n_params++;
      int read;
      if (!csi_read_parameter(param, buffer + i, &read, is_sgr)) {
        logmsg("Reject CSI: Error parsing parameter");
        c->state = CSI_REJECT;
        return i + read;
      }
      i += read;

      ch = buffer[i];
      c->state = PARAMETER(ch)      ? CSI_PARAMETER
                 : INTERMEDIATE(ch) ? CSI_INTERMEDIATE
                 : ACCEPT(ch)       ? CSI_ACCEPT
                                    : CSI_REJECT;
    } break;
    case CSI_LEADING: {
      c->leading = ch;
      ch = buffer[++i];
      c->state = PARAMETER(ch)      ? CSI_PARAMETER
                 : INTERMEDIATE(ch) ? CSI_INTERMEDIATE
                 : ACCEPT(ch)       ? CSI_ACCEPT
                                    : CSI_REJECT;
    } break;
    case CSI_INTERMEDIATE: {
      c->intermediate = ch;
      ch = buffer[++i];
      c->state = ACCEPT(ch) ? CSI_ACCEPT : CSI_REJECT;
    } break;
    case CSI_ACCEPT: {
      // Special case for empty parameter lists
      if (c->n_params == 0) {
        c->n_params = 1;
        c->params[0].primary = 0;
      }
      c->final = ch;
      return i + 1;
    } break;
    case CSI_REJECT: {
      logmsg("Reject CSI");
      return i + 1;
    } break;
    default: assert(!"Unreachable");
    }
  }
  logmsg("Reject CSI: No accept character");
  c->state = CSI_REJECT;
  return i;
}

