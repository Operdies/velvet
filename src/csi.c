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

static int csi_read_digit(const uint8_t *buffer, int *value) {
  int i, v, sign;
  i = v = 0;
  sign = 1;
  if (buffer[i] == '-') sign = -1, i++;
  while (isdigit(buffer[i])) {
    v *= 10;
    v += buffer[i] - '0';
    i++;
  }
  *value = v * sign;
  return i;
}

static bool csi_read_subparameter(const uint8_t *buffer, uint8_t separator, int *value, int *read) {
  if (buffer[0] == separator) {
    *read = csi_read_digit(buffer + 1, value) + 1;
    return true;
  }
  return false;
}

static bool csi_read_parameter(struct csi_param *param, const uint8_t *buffer, int *read, bool is_sgr) {
  int i = 0;
  if (buffer[i] == ';') i++;
  i += csi_read_digit(buffer + i, &param->primary);

  if (is_sgr) {
    // If this is an sgr sequence, we do special validation, and additionally allow subparameters to be delimited by ';'
    int n_subparameters = 0;
    int value, length, color_type;
    value = length = color_type = 0;
    bool is_custom_color = is_sgr && (param->primary == 38 || param->primary == 48);
    uint8_t separator = ':';
    int subparameter_max = LENGTH(param->sub);
    if (is_custom_color && buffer[i] == ';') {
      separator = ';';
      bool did_read = csi_read_subparameter(buffer + i, separator, &color_type, &length);
      i += length;
      if (!did_read) { 
        velvet_log("Reject SGR %d: Missing color parameter", param->primary);
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
      velvet_log("Reject CSI: Too many subparameters");
      *read = i;
      return false;
    }
    param->n_sub = n_subparameters;
  } else if (buffer[i] == ':') {
    // otherwise parse subparameter sequences only if they are delimited by ':'
    int value, length;
    value = length = 0;
    int n_subparameters = 0;
    uint8_t separator = ':';
    int subparameter_max = LENGTH(param->sub);
    while (n_subparameters < subparameter_max && csi_read_subparameter(buffer + i, separator, &value, &length)) {
      i += length;
      param->sub[n_subparameters] = value;
      n_subparameters++;
    }
    if (csi_read_subparameter(buffer + i, ':', &value, &length)) {
      velvet_log("Reject CSI: Too many subparameters");
      *read = i;
      return false;
    }
    param->n_sub = n_subparameters;
  }

  *read = i;
  return true;
}

// TODO: eww
static bool looks_like_sgr(struct u8_slice str) {
  if (str.len < 1) return false;
  bool could_be = str.content[str.len - 1] == 'm';
  if (could_be && str.len > 1) {
    uint8_t leading = str.content[0];
    uint8_t intermediate = str.content[str.len - 2];
    could_be = (leading >= '0' && leading <= '9') || leading == ';';
    could_be = could_be && ((intermediate >= '0' && intermediate <= '9') || intermediate == ';' || intermediate == ':');
  }
  return could_be;
}

int csi_parse(struct csi *c, struct u8_slice str) {
  if (str.len < 1) {
    c->state = CSI_REJECT;
    return 0;
  }
  bool is_sgr = looks_like_sgr(str);
  size_t i = 0;
  for (; i < str.len;) {
    char ch = str.content[i];
    switch (c->state) {
    case CSI_GROUND: {
      c->state = PARAMETER(ch) ? CSI_PARAMETER : INTERMEDIATE(ch) ? CSI_LEADING : ACCEPT(ch) ? CSI_ACCEPT : CSI_REJECT;
    } break;
    case CSI_PARAMETER: {
      if (c->n_params >= CSI_MAX_PARAMS) {
        c->state = CSI_REJECT;
        velvet_log("Reject CSI: Too many numeric parameters");
        return i;
      }

      struct csi_param *param = &c->params[c->n_params];
      c->n_params++;
      int read;
      if (!csi_read_parameter(param, str.content + i, &read, is_sgr)) {
        velvet_log("Reject CSI: Error parsing parameter");
        c->state = CSI_REJECT;
        return i + read;
      }
      i += read;

      ch = str.content[i];
      c->state = PARAMETER(ch)      ? CSI_PARAMETER
                 : INTERMEDIATE(ch) ? CSI_INTERMEDIATE
                 : ACCEPT(ch)       ? CSI_ACCEPT
                                    : CSI_REJECT;
    } break;
    case CSI_LEADING: {
      c->leading = ch;
      ch = str.content[++i];
      c->state = PARAMETER(ch)      ? CSI_PARAMETER
                 : INTERMEDIATE(ch) ? CSI_INTERMEDIATE
                 : ACCEPT(ch)       ? CSI_ACCEPT
                                    : CSI_REJECT;
    } break;
    case CSI_INTERMEDIATE: {
      c->intermediate = ch;
      ch = str.content[++i];
      c->state = ACCEPT(ch) ? CSI_ACCEPT : CSI_REJECT;
    } break;
    case CSI_ACCEPT: {
      c->final = ch;
      return i + 1;
    } break;
    case CSI_REJECT: {
      velvet_log("Reject CSI");
      return i + 1;
    } break;
    default: assert(!"Unreachable");
    }
  }
  velvet_log("Reject CSI: No accept character");
  c->state = CSI_REJECT;
  return i;
}

