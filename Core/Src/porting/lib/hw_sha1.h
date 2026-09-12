#ifndef __HW_SHA1_H
#define __HW_SHA1_H
#include "stm32h7xx_hal.h"
#include <sys/types.h>

#define NO_LIMIT ((ssize_t)-1)

int8_t calculate_sha1_file(const char *file_path, uint8_t *output);

/* SHA1 of at most max_bytes from file (NO_LIMIT = whole file). */
int8_t calculate_sha1_file_limit(const char *file_path, ssize_t max_bytes, uint8_t *output);

/* Get sha1 value in output (uint8_t [20]) */
int8_t calculate_sha1_hw(const uint8_t *data, size_t len, uint8_t *output);

#endif /* __HW_SHA1_H */
