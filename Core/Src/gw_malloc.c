#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "main.h"
#include "gw_malloc.h"

static uint32_t current_ram_pointer;
uint32_t ram_start;
extern uint32_t __RAM_EMU_END__;

static uint32_t current_itc_pointer;
extern uint32_t __itcram_start__;
extern uint32_t __itcram_end__;
extern uint16_t __ITCMRAM_LENGTH__;
extern uint16_t __NULLPTR_LENGTH__;

/* DTCM bump: free region from DTCM ORIGIN to the stack redzone
 * (__dtc_padding_start__ .. __dtc_padding_end__ in the linker script). */
static uint32_t current_dtcm_pointer;
extern uint32_t __dtc_padding_start__;
extern uint32_t __dtc_padding_end__;

/* Ram allocation here is simple and does not support free or reallocation.
 * Reclaim by calling the matching *_init() (or resetting ram_start). */

/* AHB: newlib heap (_heap_start.._heap_end in AHBRAM). ahb_* are aliases
 * for malloc/calloc — no pool-wide reset (launcher state must survive). */
void *ahb_malloc(size_t size)
{
  return malloc(size);
}

void *ahb_calloc(size_t count, size_t size)
{
  return calloc(count, size);
}

/* RAM_EMU bump from ram_start. Forgot by ram_init(). */
void ram_init(void)
{
  current_ram_pointer = 0;
}

size_t ram_get_free_size(void)
{
  assert(ram_start != 0);
  if (current_ram_pointer == 0)
    current_ram_pointer = (ram_start + 3) & ~0x03;
  return ((uint32_t)&__RAM_EMU_END__) - current_ram_pointer;
}

void *ram_malloc(size_t size)
{
  assert(ram_start != 0);
  if (current_ram_pointer == 0)
    current_ram_pointer = (ram_start + 3) & ~0x03;
  void *pointer = (void *)current_ram_pointer;
  if (pointer == 0)
    return NULL;
  if ((current_ram_pointer + size) <= ((uint32_t)&__RAM_EMU_END__)) {
    current_ram_pointer = (current_ram_pointer + size + 3) & ~0x03;
    return pointer;
  }
  return NULL;
}

void *ram_calloc(size_t count, size_t size)
{
  void *pointer = ram_malloc(count * size);
  if (pointer)
    memset(pointer, 0, count * size);
  return pointer;
}

/* ITC RAM is 64kB, fast; bump with no free. */

void itc_init(void)
{
  current_itc_pointer = (uint32_t)(&__itcram_end__);
}

void *itc_malloc(size_t size)
{
  void *pointer = (void *)0xffffffff;
  if (((current_itc_pointer + size + 3) & ~0x03) <=
      ((((uint32_t)&__itcram_start__) + ((uint32_t)(&__ITCMRAM_LENGTH__)) -
        ((uint32_t)(&__NULLPTR_LENGTH__))))) {
    pointer = (void *)current_itc_pointer;
    current_itc_pointer = (current_itc_pointer + size + 3) & ~0x03;
  }
  return pointer;
}

void *itc_calloc(size_t count, size_t size)
{
  void *pointer = itc_malloc(count * size);
  if (pointer != (void *)0xffffffff)
    memset(pointer, 0, count * size);
  return pointer;
}

/* DTCM bump pool (fast). Grows from DTCM ORIGIN toward the stack.
 * No per-block free — call dtcm_init() to forget everything. */

void dtcm_init(void)
{
  current_dtcm_pointer = (uint32_t)(&__dtc_padding_start__);
}

void *dtcm_malloc(size_t size)
{
  if (current_dtcm_pointer == 0)
    current_dtcm_pointer = (uint32_t)(&__dtc_padding_start__);

  uint32_t next = (current_dtcm_pointer + size + 3) & ~0x03;
  if (next > (uint32_t)&__dtc_padding_end__)
    return NULL;

  void *pointer = (void *)current_dtcm_pointer;
  current_dtcm_pointer = next;
  return pointer;
}

void *dtcm_calloc(size_t count, size_t size)
{
  size_t bytes = count * size;
  void *pointer = dtcm_malloc(bytes);
  if (pointer)
    memset(pointer, 0, bytes);
  return pointer;
}
