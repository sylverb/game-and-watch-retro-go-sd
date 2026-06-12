
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "snes.h"
#include "cpu.h"
#include "apu.h"
#include "spc.h"
#include "dma.h"
#include "ppu.h"
#include "cart.h"
#include "input.h"
#include "statehandler.h"

static const double apuCyclesPerMaster = (32040 * 32) / (1364 * 262 * 60.0);
static const double apuCyclesPerMasterPal = (32040 * 32) / (1364 * 312 * 50.0);

static void snes_runCycle(Snes* snes);
static void snes_catchupApu(Snes* snes);
static void snes_doAutoJoypad(Snes* snes);
static uint8_t snes_readReg(Snes* snes, uint16_t adr);
static void snes_writeReg(Snes* snes, uint16_t adr, uint8_t val);
static uint8_t snes_rread(Snes* snes, uint32_t adr); // wrapped by read, to set open bus
static int snes_getAccessTime(Snes* snes, uint32_t adr);

Snes* snes_init(void) {
  Snes* snes = malloc(sizeof(Snes));
  snes->cpu = cpu_init(snes, snes_cpuRead, snes_cpuWrite, snes_cpuIdle);
  snes->apu = apu_init(snes);
  snes->dma = dma_init(snes);
  snes->ppu = ppu_init(snes);
  snes->cart = cart_init(snes);
  snes->input1 = input_init(snes);
  snes->input2 = input_init(snes);
  snes->palTiming = false;
  return snes;
}

void snes_free(Snes* snes) {
  cpu_free(snes->cpu);
  apu_free(snes->apu);
  dma_free(snes->dma);
  ppu_free(snes->ppu);
  cart_free(snes->cart);
  input_free(snes->input1);
  input_free(snes->input2);
  free(snes);
}

void snes_reset(Snes* snes, bool hard) {
  cpu_reset(snes->cpu, hard);
  apu_reset(snes->apu);
  dma_reset(snes->dma);
  ppu_reset(snes->ppu);
  input_reset(snes->input1);
  input_reset(snes->input2);
  cart_reset(snes->cart);
  if(hard) memset(snes->ram, 0, sizeof(snes->ram));
  snes->ramAdr = 0;
  snes->hPos = 0;
  snes->vPos = 0;
  snes->frames = 0;
  snes->cycles = 0;
  snes->syncCycle = 0;
  snes->apuCatchupCycles = 0.0;
  snes->hIrqEnabled = false;
  snes->vIrqEnabled = false;
  snes->nmiEnabled = false;
  snes->hTimer = 0x1ff;
  snes->vTimer = 0x1ff;
  snes->inNmi = false;
  snes->irqCondition = false;
  snes->inIrq = false;
  snes->inVblank = false;
  memset(snes->portAutoRead, 0, sizeof(snes->portAutoRead));
  snes->autoJoyRead = false;
  snes->autoJoyTimer = 0;
  snes->ppuLatch = false;
  snes->multiplyA = 0xff;
  snes->multiplyResult = 0xfe01;
  snes->divideA = 0xffff;
  snes->divideResult = 0x101;
  snes->fastMem = false;
  snes->openBus = 0;
}

void snes_handleState(Snes* snes, StateHandler* sh) {
  sh_handleBools(sh,
    &snes->palTiming, &snes->hIrqEnabled, &snes->vIrqEnabled, &snes->nmiEnabled, &snes->inNmi, &snes->irqCondition,
    &snes->inIrq, &snes->inVblank, &snes->autoJoyRead, &snes->ppuLatch, &snes->fastMem, NULL
  );
  sh_handleBytes(sh, &snes->multiplyA, &snes->openBus, NULL);
  sh_handleWords(sh,
    &snes->hPos, &snes->vPos, &snes->hTimer, &snes->vTimer,
    &snes->portAutoRead[0], &snes->portAutoRead[1], &snes->portAutoRead[2], &snes->portAutoRead[3],
    &snes->autoJoyTimer, &snes->multiplyResult, &snes->divideA, &snes->divideResult, NULL
  );
  sh_handleInts(sh, &snes->ramAdr, &snes->frames, NULL);
  sh_handleLongLongs(sh, &snes->cycles, &snes->syncCycle, NULL);
  sh_handleDoubles(sh, &snes->apuCatchupCycles, NULL);
  sh_handleByteArray(sh, snes->ram, 0x20000);
  // components
  cpu_handleState(snes->cpu, sh);
  dma_handleState(snes->dma, sh);
  ppu_handleState(snes->ppu, sh);
  apu_handleState(snes->apu, sh);
  input_handleState(snes->input1, sh);
  input_handleState(snes->input2, sh);
  cart_handleState(snes->cart, sh);
}

void snes_runFrame(Snes* snes) {
  // TODO: improve handling of dma's that take up entire vblank / frame
  // run until we are starting a new frame (leaving vblank)
  while(snes->inVblank) {
    cpu_runOpcode(snes->cpu);
  }
  // then run until we are at vblank, or we end up at next frame (DMA caused vblank to be skipped)
  uint32_t frame = snes->frames;
  while(!snes->inVblank && frame == snes->frames) {
    cpu_runOpcode(snes->cpu);
  }
  snes_catchupApu(snes); // catch up the apu after running
}

void snes_runCycles(Snes* snes, int cycles) {
  if(snes->hPos + cycles >= 536 && snes->hPos < 536) {
    // if we go past 536, add 40 cycles for dram refersh
    cycles += 40;
  }
  for(int i = 0; i < cycles; i += 2) {
    snes_runCycle(snes);
  }
}

void snes_syncCycles(Snes* snes, bool start, int syncCycles) {
  if(start) {
    snes->syncCycle = snes->cycles;
    int count = syncCycles - (snes->cycles % syncCycles);
    snes_runCycles(snes, count);
  } else {
    int count = syncCycles - ((snes->cycles - snes->syncCycle) % syncCycles);
    snes_runCycles(snes, count);
  }
}

static void snes_runCycle(Snes* snes) {
  snes->apuCatchupCycles += (snes->palTiming ? apuCyclesPerMasterPal : apuCyclesPerMaster) * 2.0;
  snes->cycles += 2;
  // check for h/v timer irq's
  bool condition = (
    (snes->vIrqEnabled || snes->hIrqEnabled) &&
    (snes->vPos == snes->vTimer || !snes->vIrqEnabled) &&
    (snes->hPos == snes->hTimer * 4 || !snes->hIrqEnabled)
  );
  if(!snes->irqCondition && condition) {
    snes->inIrq = true;
    cpu_setIrq(snes->cpu, true);
  }
  snes->irqCondition = condition;
  // handle positional stuff
  if(snes->hPos == 0) {
    // end of hblank, do most vPos-tests
    bool startingVblank = false;
    if(snes->vPos == 0) {
      // end of vblank
      snes->inVblank = false;
      snes->inNmi = false;
      ppu_handleFrameStart(snes->ppu);
    } else if(snes->vPos == 225) {
      // ask the ppu if we start vblank now or at vPos 240 (overscan)
      startingVblank = !ppu_checkOverscan(snes->ppu);
    } else if(snes->vPos == 240){
      // if we are not yet in vblank, we had an overscan frame, set startingVblank
      if(!snes->inVblank) startingVblank = true;
    }
    if(startingVblank) {
      // if we are starting vblank
      ppu_handleVblank(snes->ppu);
      snes->inVblank = true;
      snes->inNmi = true;
      if(snes->autoJoyRead) {
        // TODO: this starts a little after start of vblank
        snes->autoJoyTimer = 4224;
        snes_doAutoJoypad(snes);
      }
      if(snes->nmiEnabled) {
        cpu_nmi(snes->cpu);
      }
    }
  } else if(snes->hPos == 16) {
    if(snes->vPos == 0) snes->dma->hdmaInitRequested = true;
  } else if(snes->hPos == 512) {
    // render the line halfway of the screen for better compatibility
    if(!snes->inVblank && snes->vPos > 0) ppu_runLine(snes->ppu, snes->vPos);
  } else if(snes->hPos == 1104) {
    if(!snes->inVblank) snes->dma->hdmaRunRequested = true;
  }
  // handle autoJoyRead-timer
  if(snes->autoJoyTimer > 0) snes->autoJoyTimer -= 2;
  // increment position
  snes->hPos += 2;
  if(!snes->palTiming) {
    // line 240 of odd frame with no interlace is 4 cycles shorter
    if((snes->hPos == 1360 && snes->vPos == 240 && !snes->ppu->evenFrame && !snes->ppu->frameInterlace) || snes->hPos == 1364) {
      snes->hPos = 0;
      snes->vPos++;
      // even interlace frame is 263 lines
      if((snes->vPos == 262 && (!snes->ppu->frameInterlace || !snes->ppu->evenFrame)) || snes->vPos == 263) {
        snes->vPos = 0;
        snes->frames++;
      }
    }
  } else {
    // line 311 of odd frame with interlace is 4 cycles longer
    if((snes->hPos == 1364 && (snes->vPos != 311 || snes->ppu->evenFrame || !snes->ppu->frameInterlace)) || snes->hPos == 1368) {
      snes->hPos = 0;
      snes->vPos++;
      // even interlace frame is 313 lines
      if((snes->vPos == 312 && (!snes->ppu->frameInterlace || !snes->ppu->evenFrame)) || snes->vPos == 313) {
        snes->vPos = 0;
        snes->frames++;
      }
    }
  }
}

static void snes_catchupApu(Snes* snes) {
  int catchupCycles = (int) snes->apuCatchupCycles;
  int ranCycles = apu_runCycles(snes->apu, catchupCycles);
  snes->apuCatchupCycles -= (double) ranCycles;
}

static void snes_doAutoJoypad(Snes* snes) {
  memset(snes->portAutoRead, 0, sizeof(snes->portAutoRead));
  // latch controllers
  input_latch(snes->input1, true);
  input_latch(snes->input2, true);
  input_latch(snes->input1, false);
  input_latch(snes->input2, false);
  for(int i = 0; i < 16; i++) {
    uint8_t val = input_read(snes->input1);
    snes->portAutoRead[0] |= ((val & 1) << (15 - i));
    snes->portAutoRead[2] |= (((val >> 1) & 1) << (15 - i));
    val = input_read(snes->input2);
    snes->portAutoRead[1] |= ((val & 1) << (15 - i));
    snes->portAutoRead[3] |= (((val >> 1) & 1) << (15 - i));
  }
}

uint8_t snes_readBBus(Snes* snes, uint8_t adr) {
  if(adr < 0x40) {
    return ppu_read(snes->ppu, adr);
  }
  if(adr < 0x80) {
    snes_catchupApu(snes); // catch up the apu before reading
    return snes->apu->outPorts[adr & 0x3];
  }
  if(adr == 0x80) {
    uint8_t ret = snes->ram[snes->ramAdr++];
    snes->ramAdr &= 0x1ffff;
    return ret;
  }
  return snes->openBus;
}

void snes_writeBBus(Snes* snes, uint8_t adr, uint8_t val) {
  if(adr < 0x40) {
    ppu_write(snes->ppu, adr, val);
    return;
  }
  if(adr < 0x80) {
    snes_catchupApu(snes); // catch up the apu before writing
    snes->apu->inPorts[adr & 0x3] = val;
    return;
  }
  switch(adr) {
    case 0x80: {
      snes->ram[snes->ramAdr++] = val;
      snes->ramAdr &= 0x1ffff;
      break;
    }
    case 0x81: {
      snes->ramAdr = (snes->ramAdr & 0x1ff00) | val;
      break;
    }
    case 0x82: {
      snes->ramAdr = (snes->ramAdr & 0x100ff) | (val << 8);
      break;
    }
    case 0x83: {
      snes->ramAdr = (snes->ramAdr & 0x0ffff) | ((val & 1) << 16);
      break;
    }
  }
}

static uint8_t snes_readReg(Snes* snes, uint16_t adr) {
  switch(adr) {
    case 0x4210: {
      uint8_t val = 0x2; // CPU version (4 bit)
      val |= snes->inNmi << 7;
      snes->inNmi = false;
      return val | (snes->openBus & 0x70);
    }
    case 0x4211: {
      uint8_t val = snes->inIrq << 7;
      snes->inIrq = false;
      cpu_setIrq(snes->cpu, false);
      return val | (snes->openBus & 0x7f);
    }
    case 0x4212: {
      uint8_t val = (snes->autoJoyTimer > 0);
      val |= (snes->hPos < 4 || snes->hPos >= 1096) << 6;
      val |= snes->inVblank << 7;
      return val | (snes->openBus & 0x3e);
    }
    case 0x4213: {
      return snes->ppuLatch << 7; // IO-port
    }
    case 0x4214: {
      return snes->divideResult & 0xff;
    }
    case 0x4215: {
      return snes->divideResult >> 8;
    }
    case 0x4216: {
      return snes->multiplyResult & 0xff;
    }
    case 0x4217: {
      return snes->multiplyResult >> 8;
    }
    case 0x4218:
    case 0x421a:
    case 0x421c:
    case 0x421e: {
      return snes->portAutoRead[(adr - 0x4218) / 2] & 0xff;
    }
    case 0x4219:
    case 0x421b:
    case 0x421d:
    case 0x421f: {
      return snes->portAutoRead[(adr - 0x4219) / 2] >> 8;
    }
    default: {
      return snes->openBus;
    }
  }
}

static void snes_writeReg(Snes* snes, uint16_t adr, uint8_t val) {
  switch(adr) {
    case 0x4200: {
      snes->autoJoyRead = val & 0x1;
      if(!snes->autoJoyRead) snes->autoJoyTimer = 0;
      snes->hIrqEnabled = val & 0x10;
      snes->vIrqEnabled = val & 0x20;
      if(!snes->hIrqEnabled && !snes->vIrqEnabled) {
        snes->inIrq = false;
        cpu_setIrq(snes->cpu, false);
      }
      // if nmi is enabled while inNmi is still set, immediately generate nmi
      if(!snes->nmiEnabled && (val & 0x80) && snes->inNmi) {
        cpu_nmi(snes->cpu);
      }
      snes->nmiEnabled = val & 0x80;
      break;
    }
    case 0x4201: {
      if(!(val & 0x80) && snes->ppuLatch) {
        // latch the ppu
        ppu_read(snes->ppu, 0x37);
      }
      snes->ppuLatch = val & 0x80;
      break;
    }
    case 0x4202: {
      snes->multiplyA = val;
      break;
    }
    case 0x4203: {
      snes->multiplyResult = snes->multiplyA * val;
      break;
    }
    case 0x4204: {
      snes->divideA = (snes->divideA & 0xff00) | val;
      break;
    }
    case 0x4205: {
      snes->divideA = (snes->divideA & 0x00ff) | (val << 8);
      break;
    }
    case 0x4206: {
      if(val == 0) {
        snes->divideResult = 0xffff;
        snes->multiplyResult = snes->divideA;
      } else {
        snes->divideResult = snes->divideA / val;
        snes->multiplyResult = snes->divideA % val;
      }
      break;
    }
    case 0x4207: {
      snes->hTimer = (snes->hTimer & 0x100) | val;
      break;
    }
    case 0x4208: {
      snes->hTimer = (snes->hTimer & 0x0ff) | ((val & 1) << 8);
      break;
    }
    case 0x4209: {
      snes->vTimer = (snes->vTimer & 0x100) | val;
      break;
    }
    case 0x420a: {
      snes->vTimer = (snes->vTimer & 0x0ff) | ((val & 1) << 8);
      break;
    }
    case 0x420b: {
      dma_startDma(snes->dma, val, false);
      break;
    }
    case 0x420c: {
      dma_startDma(snes->dma, val, true);
      break;
    }
    case 0x420d: {
      snes->fastMem = val & 0x1;
      break;
    }
    default: {
      break;
    }
  }
}

static uint8_t snes_rread(Snes* snes, uint32_t adr) {
  uint8_t bank = adr >> 16;
  adr &= 0xffff;
  if(bank == 0x7e || bank == 0x7f) {
    return snes->ram[((bank & 1) << 16) | adr]; // ram
  }
  if(bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) {
    if(adr < 0x2000) {
      return snes->ram[adr]; // ram mirror
    }
    if(adr >= 0x2100 && adr < 0x2200) {
      return snes_readBBus(snes, adr & 0xff); // B-bus
    }
    if(adr == 0x4016) {
      return input_read(snes->input1) | (snes->openBus & 0xfc);
    }
    if(adr == 0x4017) {
      return input_read(snes->input2) | (snes->openBus & 0xe0) | 0x1c;
    }
    if(adr >= 0x4200 && adr < 0x4220) {
      return snes_readReg(snes, adr); // internal registers
    }
    if(adr >= 0x4300 && adr < 0x4380) {
      return dma_read(snes->dma, adr); // dma registers
    }
  }
  // read from cart
  return cart_read(snes->cart, bank, adr);
}

void snes_write(Snes* snes, uint32_t adr, uint8_t val) {
  snes->openBus = val;
  uint8_t bank = adr >> 16;
  adr &= 0xffff;
  if(bank == 0x7e || bank == 0x7f) {
    snes->ram[((bank & 1) << 16) | adr] = val; // ram
  }
  if(bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) {
    if(adr < 0x2000) {
      snes->ram[adr] = val; // ram mirror
    }
    if(adr >= 0x2100 && adr < 0x2200) {
      snes_writeBBus(snes, adr & 0xff, val); // B-bus
    }
    if(adr == 0x4016) {
      input_latch(snes->input1, val & 1); // input latch
      input_latch(snes->input2, val & 1);
    }
    if(adr >= 0x4200 && adr < 0x4220) {
      snes_writeReg(snes, adr, val); // internal registers
    }
    if(adr >= 0x4300 && adr < 0x4380) {
      dma_write(snes->dma, adr, val); // dma registers
    }
  }
  // write to cart
  cart_write(snes->cart, bank, adr, val);
}

static int snes_getAccessTime(Snes* snes, uint32_t adr) {
  uint8_t bank = adr >> 16;
  adr &= 0xffff;
  if((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && adr < 0x8000) {
    // 00-3f,80-bf:0-7fff
    if(adr < 0x2000 || adr >= 0x6000) return 8; // 0-1fff, 6000-7fff
    if(adr < 0x4000 || adr >= 0x4200) return 6; // 2000-3fff, 4200-5fff
    return 12; // 4000-41ff
  }
  // 40-7f,co-ff:0000-ffff, 00-3f,80-bf:8000-ffff
  return (snes->fastMem && bank >= 0x80) ? 6 : 8; // depends on setting in banks 80+
}

uint8_t snes_read(Snes* snes, uint32_t adr) {
  uint8_t val = snes_rread(snes, adr);
  snes->openBus = val;
  return val;
}

void snes_cpuIdle(void* mem, bool waiting) {
  Snes* snes = (Snes*) mem;
  dma_handleDma(snes->dma, 6);
  snes_runCycles(snes, 6);
}

uint8_t snes_cpuRead(void* mem, uint32_t adr) {
  Snes* snes = (Snes*) mem;
  int cycles = snes_getAccessTime(snes, adr);
  dma_handleDma(snes->dma, cycles);
  snes_runCycles(snes, cycles);
  return snes_read(snes, adr);
}

void snes_cpuWrite(void* mem, uint32_t adr, uint8_t val) {
  Snes* snes = (Snes*) mem;
  int cycles = snes_getAccessTime(snes, adr);
  dma_handleDma(snes->dma, cycles);
  snes_runCycles(snes, cycles);
  snes_write(snes, adr, val);
}

// debugging

void snes_runCpuCycle(Snes* snes) {
  cpu_runOpcode(snes->cpu);
}

void snes_runSpcCycle(Snes* snes) {
  // TODO: apu catchup is not aware of this, SPC runs extra cycle(s)
  spc_runOpcode(snes->apu->spc);
}
