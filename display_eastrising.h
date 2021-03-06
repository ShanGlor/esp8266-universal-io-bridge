#ifndef display_eastrising_h
#define display_eastrising_h

#include <stdint.h>
#include <stdbool.h>

bool display_eastrising_init(void);
bool display_eastrising_begin(int slot, bool logmode);
bool display_eastrising_output(unsigned int);
bool display_eastrising_end(void);
bool display_eastrising_bright(int);
bool display_eastrising_standout(bool);
bool display_eastrising_periodic(void);
bool display_eastrising_picture_load(unsigned int);
bool display_eastrising_layer_select(unsigned int);

#endif
