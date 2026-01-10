#include "velvet.h"
#include <sys/stat.h>
#include "velvet_api.h"

/* Get Time in milliseconds before pending keybinds time out */
int vv_api_get_key_repeat_timeout(struct velvet *v) {
  return v->input.options.key_repeat_timeout_ms;
}
/* Set Time in milliseconds before pending keybinds time out */
int vv_api_set_key_repeat_timeout(struct velvet *v, int new_value) {
  v->input.options.key_repeat_timeout_ms = new_value;
  return v->input.options.key_repeat_timeout_ms;
}

/* Get Bitmask of the currently visible tags. */
int vv_api_get_view(struct velvet *v) {
  return v->scene.view;
}
/* Set Bitmask of the currently visible tags. */
int vv_api_set_view(struct velvet *v, int new_value) {
  velvet_scene_set_view(&v->scene, new_value);
  return v->scene.view;
}
int vv_api_get_tags(struct velvet *vv, int *winid){
  return velvet_scene_get_tags_for_window(&vv->scene, winid ? *winid : 0);
}
int vv_api_set_tags(struct velvet *vv, int tags, int *winid){
  velvet_scene_set_tags_for_window(&vv->scene, winid ? *winid : 0, tags);
  return vv_api_get_tags(vv, winid);
}

int vv_api_spawn(struct velvet *vv, const char* cmd, int* left, int* top, int* width, int* height, const char** layer) {
  struct rect initial_size = {
    .x = left ? *left : 10,
    .y = top ? *top : 5,
    .w = width ? *width : 80,
    .h = height ? *height : 24,
  };
  struct velvet_window template = {
    .emulator = vte_default,
    .border_width = 1,
    .layer = VELVET_LAYER_TILED,
    .rect.window = initial_size,
  };
  string_push_cstr(&template.cmdline, cmd);

  if (layer && *layer) {
    struct u8_slice S = u8_slice_from_cstr(*layer);
    if (u8_match(S, "floating")) {
      template.layer = VELVET_LAYER_FLOATING;
    } else if (u8_match(S, "tiled")) {
      template.layer = VELVET_LAYER_TILED;
    } else if (u8_match(S, "background")) {
      template.layer = VELVET_LAYER_BACKGROUND;
    }
  }

  return (int)velvet_scene_spawn_process_from_template(&vv->scene, template);
}
