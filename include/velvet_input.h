#ifndef VELVET_INPUT_H
#define VELVET_INPUT_H

#include "collections.h"
#include "multiplexer.h"

struct velvet_input_options {
  bool focus_follows_mouse;
};

struct velvet_keymap ;
typedef bool (on_key)(struct velvet_keymap *k, uint8_t ch);

struct velvet_keymap {
  /* the root keymap in this tree */
  struct velvet_keymap *root;
  /* the next keymap in the same level */
  struct velvet_keymap *next;
  /* the new keymap after this keymap is activated. If null, this keymap remains active. */
  struct velvet_keymap *chain;
  /* callback called when this keymap is activated, and on every subsequent keystroke.
   * return true to reset the keymap to the root keymap.
   */
  on_key *on_key;
  void *data;
  uint8_t key;
};

struct velvet_input {
  struct multiplexer *m;
  struct string command_buffer;
  struct velvet_keymap *keymap;
  struct velvet_input_options options;
};


void velvet_input_process(struct velvet_input *in, struct u8_slice str);
void velvet_input_destroy(struct velvet_input *in);
void velvet_input_add_default_binds(struct velvet_input *in);
struct velvet_keymap * velvet_input_add_keybind(struct velvet_input *in, struct u8_slice keys, on_key *callback, void *data);

#endif // VELVET_INPUT_H
