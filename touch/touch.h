#ifndef TOUCH_H
#define TOUCH_H

#include <stdbool.h>

/* Initialize I2C3 and STMPE811 touch controller */
void touch_init(void);

/* Poll STMPE811 for touch coordinates 
 * Returns true if a touch is currently detected, false otherwise.
 * Overwrites x and y with display-mapped coordinates if true.
 */
bool touch_read(int *x, int *y);

/* Empty the FIFO */
void touch_clear_fifo(void);

#endif /* TOUCH_H */
