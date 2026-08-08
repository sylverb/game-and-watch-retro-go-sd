#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "main.h"
#include "gw_malloc.h"

static uint32_t current_ram_pointer;
uint32_t ram_start;
extern uint32_t __RAM_EMU_END__;

static uint32_t current_ahb_pointer;
extern uint32_t __ahbram_heap_start__;
extern uint32_t __ahbram_audio_start__;

static uint32_t current_itc_pointer;
extern uint32_t __itcram_start__;
extern uint32_t __itcram_end__;
extern uint16_t __ITCMRAM_LENGTH__;
extern uint16_t __NULLPTR_LENGTH__;

/* Ram allocation here is simple and does not support free or reallocation  */
/* What is possible is to reinitialize all allocated buffers by calling the */
/* AHB/ITC init function.                                                   */

/* AHB SRAM: ~120 KB at the bottom is D-cacheable (MPU PRIVDEF). The last 8 KB
   hold the .audio DMA buffer and stay non-cacheable. */
void ahb_init() {
  current_ram_pointer = (uint32_t)0;
  current_ahb_pointer = (uint32_t)(&__ahbram_heap_start__);
}

size_t ram_get_free_size() {
  assert(ram_start != 0); // We are not supposed to ram alloc without initializing ram start pointer
  if (current_ram_pointer == 0)
    current_ram_pointer = (ram_start + 3) & ~0x03; // Make sure pointers are always 32 bits aligned;
  return ((uint32_t)&__RAM_EMU_END__) - current_ram_pointer;
}

void *ram_malloc(size_t size) {
  assert(ram_start != 0); // We are not supposed to ram alloc without initializing ram start pointer
  if (current_ram_pointer == 0)
    current_ram_pointer = (ram_start + 3) & ~0x03; // Make sure pointers are always 32 bits aligned;
//  printf("ram_malloc 0x%lx size %d\n",current_ram_pointer,size);
  void *pointer = (void *)current_ram_pointer;
  if (pointer == 0)
    return NULL;
  if ((current_ram_pointer + size) <= ((uint32_t)&__RAM_EMU_END__)) {
    current_ram_pointer = (current_ram_pointer + size + 3) & ~0x03; // Make sure pointers are always 32 bits aligned
    return pointer;
  } else {
    return NULL;
  }
}

void *ram_calloc(size_t count,size_t size) {
  void *pointer = ram_malloc(count*size);
  if (pointer)
    memset(pointer,0,count*size);
  return pointer;
}

void *ahb_malloc(size_t size) {
  void *pointer;
  if (current_ram_pointer != 0)
  {
    pointer = ram_malloc(size);
    if (pointer)
      return pointer;
  }
  return ahb_only_malloc(size);
}

void *ahb_only_malloc(size_t size) {
  void *pointer = (void *)current_ahb_pointer;
  current_ahb_pointer = (current_ahb_pointer + size + 3) & ~0x03;
  assert(current_ahb_pointer <= (uint32_t)&__ahbram_audio_start__);
  return pointer;
}

void *ahb_calloc(size_t count,size_t size) {
  size_t bytes = count * size;
  void *pointer = ram_malloc(bytes);
  if (pointer == NULL)
    pointer = ahb_malloc(bytes);
  if (pointer == NULL)
    return NULL;
  memset(pointer, 0, bytes);
  return pointer;
}

/* ITC RAM is 64kB, it's fast RAM and can be used for any purpose */

void itc_init() {
  current_itc_pointer = (uint32_t)(&__itcram_end__);
  /* Cores that park .text in ITCM (MSX) redirect former itc_malloc traffic
   * to the DTCM arena — reset that bump here too so board rebuilds inside
   * a running core (e.g. MSX machine switch) reclaim the arena. Harmless
   * for cores that never touch GW_MEM_DTCM_ARENA. */
  dtcm_arena_init();
}

void *itc_malloc(size_t size) {
  // ITC ram start at 0x00000000 so we can't use NULL value to tell if allocation is not possible.
  void *pointer = (void *)0xffffffff;
//  void *pointer = (void *)current_itc_pointer;
//  printf("itc_malloc 0x%lx size %d\n",current_itc_pointer,size);
  if (((current_itc_pointer + size + 3) & ~0x03) <= ((((uint32_t)&__itcram_start__) + ((uint32_t)(&__ITCMRAM_LENGTH__)) - ((uint32_t)(&__NULLPTR_LENGTH__))))) {
    pointer = (void *)current_itc_pointer;
    current_itc_pointer = (current_itc_pointer + size + 3) & ~0x03; // Make sure pointers are always 32 bits aligned;
  }
//  assert((current_itc_pointer) <= ((((uint32_t)&__itcram_start__) + ((uint32_t)(&__ITCMRAM_LENGTH__)) - ((uint32_t)(&__NULLPTR_LENGTH__)))));
  return pointer;
}

void *itc_calloc(size_t count,size_t size) {
  void *pointer = itc_malloc(count*size);
  if (pointer != (void *)0xffffffff)
    memset(pointer,0,count*size);
  return pointer;
}

/* DTCM stdlib heap — the linker places _heap_start/_heap_end in DTCMRAM. */
void *
dtcm_malloc(size_t size)
{
  printf("dtcm_malloc 0x%x\n", size);
	return malloc(size);
}

void *
dtcm_calloc(size_t count, size_t size)
{
  printf("dtcm_calloc 0x%x\n", size*count);
	return calloc(count, size);
}

void
dtcm_free(void *ptr)
{
  printf("dtcm_free %p\n", ptr);
	free(ptr);
}

/* ---- DTCM bump arena (for cores that need ITCM for code) ----------------
 * blueMSX historically parked mapper/VDP/audio structs in ITCM via
 * itc_malloc. When those cores move hot .text into ITCM, that heap must
 * live elsewhere. A 64KB bump arena carved once from the DTCM newlib heap
 * gives the same "reset every launch" semantics as itc_init without
 * fighting the ITCM code segment. */

#define DTCM_ARENA_SIZE (64u * 1024u)

static uint8_t *dtcm_arena_base;
static uint32_t dtcm_arena_used;

void dtcm_arena_init(void)
{
  if (dtcm_arena_base == NULL)
    dtcm_arena_base = (uint8_t *)malloc(DTCM_ARENA_SIZE);
  dtcm_arena_used = 0;
}

void *dtcm_arena_malloc(size_t size)
{
  void *pointer = (void *)0xffffffff;
  size_t aligned = (size + 3u) & ~3u;

  if (dtcm_arena_base != NULL &&
      dtcm_arena_used + aligned <= DTCM_ARENA_SIZE) {
    pointer = dtcm_arena_base + dtcm_arena_used;
    dtcm_arena_used += (uint32_t)aligned;
  }
  return pointer;
}

void *dtcm_arena_calloc(size_t count, size_t size)
{
  size_t bytes = count * size;
  void *pointer = dtcm_arena_malloc(bytes);
  if (pointer != (void *)0xffffffff)
    memset(pointer, 0, bytes);
  return pointer;
}
