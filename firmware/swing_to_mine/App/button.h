// Blue user button (B1, PA0, active high), debounced.
#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>
#include <stdint.h>

void button_init(void);

// Call often (every ms or so). Returns the debounced state: true = held down.
bool button_poll(uint32_t now_ms);

#endif
