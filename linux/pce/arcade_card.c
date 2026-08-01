/* Arcade Card emulation (enabled with -DPCE_ENABLE_ARCADE_CARD).
 * Ported from Mednafen arcade_card.cpp — Ki / David Shadoff documentation. */

#include "arcade_card.h"
#include "pce.h"

#include <stdlib.h>
#include <string.h>

uint8_t PCE_ACAREA_MARKER[1];

typedef struct {
	uint32_t base;
	uint16_t offset;
	uint16_t increment;
	uint8_t  control;
} ac_port_t;

typedef struct {
	ac_port_t ports[4];
	uint32_t shift_latch;
	uint8_t  shift_bits;
	uint8_t  rotate_bits;
} ac_regs_t;

static ac_regs_t ac;
static uint8_t *acram;
static bool acram_used;
static void ac_auto_increment(ac_port_t *port)
{
	if (!(port->control & 0x1))
		return;

	if (port->control & 0x10)
		port->base = (port->base + port->increment) & 0xFFFFFF;
	else
		port->offset = (port->offset + port->increment) & 0xFFFF;
}

static uint32_t ac_port_index(uint16_t addr)
{
	return (addr >> 4) & 0x3;
}

static uint32_t ac_ram_addr(const ac_port_t *port)
{
	uint32_t aci = port->base;

	if (port->control & 0x2) {
		aci += port->offset;
		if (port->control & 0x8)
			aci += 0xFF0000;
	}
	return aci & 0x1FFFFF;
}

void pce_arcade_card_init(void)
{
	if (!acram)
		acram = (uint8_t *)malloc(0x200000);
	memset(&ac, 0, sizeof(ac));
	acram_used = false;
	if (acram)
		memset(acram, 0, 0x200000);
}

void pce_arcade_card_shutdown(void)
{
	free(acram);
	acram = NULL;
	acram_used = false;
}

void pce_arcade_card_reset(void)
{
	if (acram)
		memset(acram, 0, 0x200000);
	memset(&ac, 0, sizeof(ac));
	acram_used = false;
}

void pce_arcade_card_map_banks(void)
{
	for (int v = 0x40; v < 0x44; v++) {
		PCE.MemoryMapR[v] = PCE_ACAREA_MARKER;
		PCE.MemoryMapW[v] = PCE_ACAREA_MARKER;
	}
	for (int p = 0; p < 8; p++) {
		if (PCE.MMR[p] >= 0x40 && PCE.MMR[p] <= 0x43)
			pce_bank_set((uint8_t)p, PCE.MMR[p]);
	}
}

uint8_t pce_arcade_card_read(uint16_t A)
{
	if ((A & 0x1F00) != 0x1A00)
		return 0xFF;
	if (!acram)
		return 0xFF;

	if (A < 0x1A80) {
		ac_port_t *port = &ac.ports[ac_port_index(A)];

		switch (A & 0xF) {
		case 0x00:
		case 0x01: {
			uint8_t ret = acram[ac_ram_addr(port)];
			ac_auto_increment(port);
			return ret;
		}
		case 0x02: return (uint8_t)(port->base >> 0);
		case 0x03: return (uint8_t)(port->base >> 8);
		case 0x04: return (uint8_t)(port->base >> 16);
		case 0x05: return (uint8_t)(port->offset >> 0);
		case 0x06: return (uint8_t)(port->offset >> 8);
		case 0x07: return (uint8_t)(port->increment >> 0);
		case 0x08: return (uint8_t)(port->increment >> 8);
		case 0x09: return port->control;
		default: break;
		}
	} else if (A >= 0x1AE0) {
		switch (A & 0x1F) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03: return (uint8_t)((ac.shift_latch >> ((A & 3) * 8)) & 0xFF);
		case 0x04: return ac.shift_bits;
		case 0x05: return ac.rotate_bits;
		case 0x1C:
		case 0x1D: return 0x00;
		case 0x1E: return 0x10; /* version */
		case 0x1F: return 0x51; /* Arcade Card ID */
		default: break;
		}
	}
	return 0xFF;
}

void pce_arcade_card_write(uint16_t A, uint8_t V)
{
	if ((A & 0x1F00) != 0x1A00 || !acram)
		return;

	if (A < 0x1A80) {
		ac_port_t *port = &ac.ports[ac_port_index(A)];

		switch (A & 0xF) {
		case 0x00:
		case 0x01: {
			uint32_t aci = ac_ram_addr(port);
			acram[aci] = V;
			acram_used = true;
			ac_auto_increment(port);
			break;
		}
		case 0x02:
			port->base &= ~0xFFu;
			port->base |= (uint32_t)V;
			break;
		case 0x03:
			port->base &= ~0xFF00u;
			port->base |= (uint32_t)V << 8;
			break;
		case 0x04:
			port->base &= ~0xFF0000u;
			port->base |= (uint32_t)V << 16;
			break;
		case 0x05:
			port->offset &= ~0xFFu;
			port->offset |= V;
			if ((port->control & 0x60) == 0x20) {
				if (port->control & 0x08)
					port->base += 0xFF0000;
				port->base = (port->base + port->offset) & 0xFFFFFF;
			}
			break;
		case 0x06:
			port->offset &= ~0xFF00u;
			port->offset |= (uint16_t)V << 8;
			if ((port->control & 0x60) == 0x40) {
				if (port->control & 0x08)
					port->base += 0xFF0000;
				port->base = (port->base + port->offset) & 0xFFFFFF;
			}
			break;
		case 0x07:
			port->increment &= ~0xFFu;
			port->increment |= V;
			break;
		case 0x08:
			port->increment &= ~0xFF00u;
			port->increment |= (uint16_t)V << 8;
			break;
		case 0x09:
			port->control = V & 0x7F;
			break;
		case 0x0A:
			if ((port->control & 0x60) == 0x60) {
				if (port->control & 0x08)
					port->base += 0xFF0000;
				port->base = (port->base + port->offset) & 0xFFFFFF;
			}
			break;
		default:
			break;
		}
	} else if (A >= 0x1AE0) {
		switch (A & 0x1F) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
			ac.shift_latch &= ~(0xFFu << ((A & 3) * 8));
			ac.shift_latch |= (uint32_t)V << ((A & 3) * 8);
			break;
		case 0x04:
			ac.shift_bits = V & 0xF;
			if (ac.shift_bits) {
				if (ac.shift_bits & 0x8)
					ac.shift_latch >>= 16 - ac.shift_bits;
				else
					ac.shift_latch <<= ac.shift_bits;
			}
			break;
		case 0x05:
			ac.rotate_bits = V & 0xF;
			if (ac.rotate_bits) {
				if (ac.rotate_bits & 0x8) {
					unsigned sa = 16 - ac.rotate_bits;
					unsigned orv = ac.shift_latch << (32 - sa);
					ac.shift_latch = (ac.shift_latch >> sa) | orv;
				} else {
					unsigned sa = ac.rotate_bits;
					unsigned orv = (ac.shift_latch >> (32 - sa)) & ((1u << sa) - 1);
					ac.shift_latch = (ac.shift_latch << sa) | orv;
				}
			}
			break;
		default:
			break;
		}
	}
}

uint8_t pce_arcade_card_phys_read(uint32_t phys_addr)
{
	return pce_arcade_card_read((uint16_t)(0x1A00 | ((phys_addr >> 9) & 0x30u)));
}

void pce_arcade_card_phys_write(uint32_t phys_addr, uint8_t val)
{
	pce_arcade_card_write((uint16_t)(0x1A00 | ((phys_addr >> 9) & 0x30u)), val);
}

void pce_arcade_card_state_get(pce_arcade_card_state_t *st)
{
	memset(st, 0, sizeof(*st));
	for (int i = 0; i < 4; i++) {
		st->ports_base[i] = ac.ports[i].base;
		st->ports_offset[i] = ac.ports[i].offset;
		st->ports_increment[i] = ac.ports[i].increment;
		st->ports_control[i] = ac.ports[i].control;
	}
	st->shift_latch = ac.shift_latch;
	st->shift_bits = ac.shift_bits;
	st->rotate_bits = ac.rotate_bits;
	st->ram_used = acram_used ? 1 : 0;
}

void pce_arcade_card_state_set(const pce_arcade_card_state_t *st)
{
	for (int i = 0; i < 4; i++) {
		ac.ports[i].base = st->ports_base[i];
		ac.ports[i].offset = st->ports_offset[i];
		ac.ports[i].increment = st->ports_increment[i];
		ac.ports[i].control = st->ports_control[i];
	}
	ac.shift_latch = st->shift_latch;
	ac.shift_bits = st->shift_bits;
	ac.rotate_bits = st->rotate_bits;
	acram_used = st->ram_used != 0;
}

uint8_t *pce_arcade_card_ram(void)
{
	return acram;
}

bool pce_arcade_card_ram_used(void)
{
	return acram_used;
}
