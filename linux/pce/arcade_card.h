#ifndef PCE_ARCADE_CARD_H
#define PCE_ARCADE_CARD_H

#include <stdint.h>
#include <stdbool.h>

/* Sentinel pointer — MemoryMapR/W[0x40-0x43] point here on Linux when the
 * Arcade Card physical window is mapped (mirrors Mednafen AC_PhysRead/Write). */
extern uint8_t PCE_ACAREA_MARKER[1];

void pce_arcade_card_init(void);
void pce_arcade_card_shutdown(void);
void pce_arcade_card_reset(void);
void pce_arcade_card_map_banks(void);

uint8_t pce_arcade_card_read(uint16_t addr);
void    pce_arcade_card_write(uint16_t addr, uint8_t val);

uint8_t pce_arcade_card_phys_read(uint32_t phys_addr);
void    pce_arcade_card_phys_write(uint32_t phys_addr, uint8_t val);

/* Savestate blob after SCSI block: 'ARCD' + regs + optional 2MB RAM. */
#define PCE_ARCADE_CARD_STATE_MAGIC 0x44524341u /* 'ARCD' */

typedef struct {
	uint32_t ports_base[4];
	uint16_t ports_offset[4];
	uint16_t ports_increment[4];
	uint8_t  ports_control[4];
	uint32_t shift_latch;
	uint8_t  shift_bits;
	uint8_t  rotate_bits;
	uint8_t  ram_used;
} pce_arcade_card_state_t;

void pce_arcade_card_state_get(pce_arcade_card_state_t *st);
void pce_arcade_card_state_set(const pce_arcade_card_state_t *st);

uint8_t *pce_arcade_card_ram(void);
bool     pce_arcade_card_ram_used(void);

#endif
