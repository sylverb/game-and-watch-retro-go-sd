
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "dma.h"
#include "snes.h"
#include "statehandler.h"

static const int bAdrOffsets[8][4] = {
  {0, 0, 0, 0},
  {0, 1, 0, 1},
  {0, 0, 0, 0},
  {0, 0, 1, 1},
  {0, 1, 2, 3},
  {0, 1, 0, 1},
  {0, 0, 0, 0},
  {0, 0, 1, 1}
};

static const int transferLength[8] = {
  1, 2, 2, 4, 4, 4, 2, 4
};

static void dma_transferByte(Dma* dma, uint16_t aAdr, uint8_t aBank, uint8_t bAdr, bool fromB);
static void dma_waitCycle(Dma* dma);
static void dma_doDma(Dma* dma, int cpuCycles);
static void dma_initHdma(Dma* dma, bool doSync, int cpuCycles);
static void dma_doHdma(Dma* dma, bool doSync, int cpuCycles);

Dma* dma_init(Snes* snes) {
  Dma* dma = malloc(sizeof(Dma));
  dma->snes = snes;
  return dma;
}

void dma_free(Dma* dma) {
  free(dma);
}

void dma_reset(Dma* dma) {
  for(int i = 0; i < 8; i++) {
    dma->channel[i].bAdr = 0xff;
    dma->channel[i].aAdr = 0xffff;
    dma->channel[i].aBank = 0xff;
    dma->channel[i].size = 0xffff;
    dma->channel[i].indBank = 0xff;
    dma->channel[i].tableAdr = 0xffff;
    dma->channel[i].repCount = 0xff;
    dma->channel[i].unusedByte = 0xff;
    dma->channel[i].dmaActive = false;
    dma->channel[i].hdmaActive = false;
    dma->channel[i].mode = 7;
    dma->channel[i].fixed = true;
    dma->channel[i].decrement = true;
    dma->channel[i].indirect = true;
    dma->channel[i].fromB = true;
    dma->channel[i].unusedBit = true;
    dma->channel[i].doTransfer = false;
    dma->channel[i].terminated = false;
  }
  dma->dmaState = 0;
  dma->hdmaInitRequested = false;
  dma->hdmaRunRequested = false;
}

void dma_handleState(Dma* dma, StateHandler* sh) {
  sh_handleBools(sh, &dma->hdmaInitRequested, &dma->hdmaRunRequested, NULL);
  sh_handleBytes(sh, &dma->dmaState, NULL);
  for(int i = 0; i < 8; i++) {
    sh_handleBools(sh,
      &dma->channel[i].dmaActive, &dma->channel[i].hdmaActive, &dma->channel[i].fixed, &dma->channel[i].decrement,
      &dma->channel[i].indirect, &dma->channel[i].fromB, &dma->channel[i].unusedBit, &dma->channel[i].doTransfer,
      &dma->channel[i].terminated, NULL
    );
    sh_handleBytes(sh,
      &dma->channel[i].bAdr, &dma->channel[i].aBank, &dma->channel[i].indBank, &dma->channel[i].repCount,
      &dma->channel[i].unusedByte, &dma->channel[i].mode, NULL
    );
    sh_handleWords(sh, &dma->channel[i].aAdr, &dma->channel[i].size, &dma->channel[i].tableAdr, NULL);
  }
}

uint8_t dma_read(Dma* dma, uint16_t adr) {
  uint8_t c = (adr & 0x70) >> 4;
  switch(adr & 0xf) {
    case 0x0: {
      uint8_t val = dma->channel[c].mode;
      val |= dma->channel[c].fixed << 3;
      val |= dma->channel[c].decrement << 4;
      val |= dma->channel[c].unusedBit << 5;
      val |= dma->channel[c].indirect << 6;
      val |= dma->channel[c].fromB << 7;
      return val;
    }
    case 0x1: {
      return dma->channel[c].bAdr;
    }
    case 0x2: {
      return dma->channel[c].aAdr & 0xff;
    }
    case 0x3: {
      return dma->channel[c].aAdr >> 8;
    }
    case 0x4: {
      return dma->channel[c].aBank;
    }
    case 0x5: {
      return dma->channel[c].size & 0xff;
    }
    case 0x6: {
      return dma->channel[c].size >> 8;
    }
    case 0x7: {
      return dma->channel[c].indBank;
    }
    case 0x8: {
      return dma->channel[c].tableAdr & 0xff;
    }
    case 0x9: {
      return dma->channel[c].tableAdr >> 8;
    }
    case 0xa: {
      return dma->channel[c].repCount;
    }
    case 0xb:
    case 0xf: {
      return dma->channel[c].unusedByte;
    }
    default: {
      return dma->snes->openBus;
    }
  }
}

void dma_write(Dma* dma, uint16_t adr, uint8_t val) {
  uint8_t c = (adr & 0x70) >> 4;
  switch(adr & 0xf) {
    case 0x0: {
      dma->channel[c].mode = val & 0x7;
      dma->channel[c].fixed = val & 0x8;
      dma->channel[c].decrement = val & 0x10;
      dma->channel[c].unusedBit = val & 0x20;
      dma->channel[c].indirect = val & 0x40;
      dma->channel[c].fromB = val & 0x80;
      break;
    }
    case 0x1: {
      dma->channel[c].bAdr = val;
      break;
    }
    case 0x2: {
      dma->channel[c].aAdr = (dma->channel[c].aAdr & 0xff00) | val;
      break;
    }
    case 0x3: {
      dma->channel[c].aAdr = (dma->channel[c].aAdr & 0xff) | (val << 8);
      break;
    }
    case 0x4: {
      dma->channel[c].aBank = val;
      break;
    }
    case 0x5: {
      dma->channel[c].size = (dma->channel[c].size & 0xff00) | val;
      break;
    }
    case 0x6: {
      dma->channel[c].size = (dma->channel[c].size & 0xff) | (val << 8);
      break;
    }
    case 0x7: {
      dma->channel[c].indBank = val;
      break;
    }
    case 0x8: {
      dma->channel[c].tableAdr = (dma->channel[c].tableAdr & 0xff00) | val;
      break;
    }
    case 0x9: {
      dma->channel[c].tableAdr = (dma->channel[c].tableAdr & 0xff) | (val << 8);
      break;
    }
    case 0xa: {
      dma->channel[c].repCount = val;
      break;
    }
    case 0xb:
    case 0xf: {
      dma->channel[c].unusedByte = val;
      break;
    }
    default: {
      break;
    }
  }
}

static void dma_waitCycle(Dma* dma) {
  // run hdma if requested, no sync (already sycned due to dma)
  if(dma->hdmaInitRequested) dma_initHdma(dma, false, 0);
  if(dma->hdmaRunRequested) dma_doHdma(dma, false, 0);
  snes_runCycles(dma->snes, 8);
}

static void dma_doDma(Dma* dma, int cpuCycles) {
  // align to multiple of 8
  snes_syncCycles(dma->snes, true, 8);
  // full transfer overhead
  dma_waitCycle(dma);
  for(int i = 0; i < 8; i++) {
    if(!dma->channel[i].dmaActive) continue;
    // do channel i
    dma_waitCycle(dma); // overhead per channel
    int offIndex = 0;
    while(dma->channel[i].dmaActive) {
      dma_waitCycle(dma);
      dma_transferByte(
        dma, dma->channel[i].aAdr, dma->channel[i].aBank,
        dma->channel[i].bAdr + bAdrOffsets[dma->channel[i].mode][offIndex++], dma->channel[i].fromB
      );
      offIndex &= 3;
      if(!dma->channel[i].fixed) {
        dma->channel[i].aAdr += dma->channel[i].decrement ? -1 : 1;
      }
      dma->channel[i].size--;
      if(dma->channel[i].size == 0) {
        dma->channel[i].dmaActive = false;
      }
    }
  }
  // re-align to cpu cycles
  snes_syncCycles(dma->snes, false, cpuCycles);
}

static void dma_initHdma(Dma* dma, bool doSync, int cpuCycles) {
  dma->hdmaInitRequested = false;
  bool hdmaEnabled = false;
  // check if a channel is enabled, and do reset
  for(int i = 0; i < 8; i++) {
    if(dma->channel[i].hdmaActive) hdmaEnabled = true;
    dma->channel[i].doTransfer = false;
    dma->channel[i].terminated = false;
  }
  if(!hdmaEnabled) return;
  if(doSync) snes_syncCycles(dma->snes, true, 8);
  // full transfer overhead
  snes_runCycles(dma->snes, 8);
  for(int i = 0; i < 8; i++) {
    if(dma->channel[i].hdmaActive) {
      // terminate any dma
      dma->channel[i].dmaActive = false;
      // load address, repCount, and indirect address if needed
      snes_runCycles(dma->snes, 8);
      dma->channel[i].tableAdr = dma->channel[i].aAdr;
      dma->channel[i].repCount = snes_read(dma->snes, (dma->channel[i].aBank << 16) | dma->channel[i].tableAdr++);
      if(dma->channel[i].repCount == 0) dma->channel[i].terminated = true;
      if(dma->channel[i].indirect) {
        snes_runCycles(dma->snes, 8);
        dma->channel[i].size = snes_read(dma->snes, (dma->channel[i].aBank << 16) | dma->channel[i].tableAdr++);
        snes_runCycles(dma->snes, 8);
        dma->channel[i].size |= snes_read(dma->snes, (dma->channel[i].aBank << 16) | dma->channel[i].tableAdr++) << 8;
      }
      dma->channel[i].doTransfer = true;
    }
  }
  if(doSync) snes_syncCycles(dma->snes, false, cpuCycles);
}

static void dma_doHdma(Dma* dma, bool doSync, int cpuCycles) {
  dma->hdmaRunRequested = false;
  bool hdmaActive = false;
  int lastActive = 0;
  for(int i = 0; i < 8; i++) {
    if(dma->channel[i].hdmaActive) {
      hdmaActive = true;
      if(!dma->channel[i].terminated) lastActive = i;
    }
  }
  if(!hdmaActive) return;
  if(doSync) snes_syncCycles(dma->snes, true, 8);
  // full transfer overhead
  snes_runCycles(dma->snes, 8);
  // do all copies
  for(int i = 0; i < 8; i++) {
    // terminate any dma
    if(dma->channel[i].hdmaActive) dma->channel[i].dmaActive = false;
    if(dma->channel[i].hdmaActive && !dma->channel[i].terminated) {
      // do the hdma
      if(dma->channel[i].doTransfer) {
        for(int j = 0; j < transferLength[dma->channel[i].mode]; j++) {
          snes_runCycles(dma->snes, 8);
          if(dma->channel[i].indirect) {
            dma_transferByte(
              dma, dma->channel[i].size++, dma->channel[i].indBank,
              dma->channel[i].bAdr + bAdrOffsets[dma->channel[i].mode][j], dma->channel[i].fromB
            );
          } else {
            dma_transferByte(
              dma, dma->channel[i].tableAdr++, dma->channel[i].aBank,
              dma->channel[i].bAdr + bAdrOffsets[dma->channel[i].mode][j], dma->channel[i].fromB
            );
          }
        }
      }
    }
  }
  // do all updates
  for(int i = 0; i < 8; i++) {
    if(dma->channel[i].hdmaActive && !dma->channel[i].terminated) {
      dma->channel[i].repCount--;
      dma->channel[i].doTransfer = dma->channel[i].repCount & 0x80;
      snes_runCycles(dma->snes, 8);
      uint8_t newRepCount = snes_read(dma->snes, (dma->channel[i].aBank << 16) | dma->channel[i].tableAdr);
      if((dma->channel[i].repCount & 0x7f) == 0) {
        dma->channel[i].repCount = newRepCount;
        dma->channel[i].tableAdr++;
        if(dma->channel[i].indirect) {
          if(dma->channel[i].repCount == 0 && i == lastActive) {
            // if this is the last active channel, only fetch high, and use 0 for low
            dma->channel[i].size = 0;
          } else {
            snes_runCycles(dma->snes, 8);
            dma->channel[i].size = snes_read(dma->snes, (dma->channel[i].aBank << 16) | dma->channel[i].tableAdr++);
          }
          snes_runCycles(dma->snes, 8);
          dma->channel[i].size |= snes_read(dma->snes, (dma->channel[i].aBank << 16) | dma->channel[i].tableAdr++) << 8;
        }
        if(dma->channel[i].repCount == 0) dma->channel[i].terminated = true;
        dma->channel[i].doTransfer = true;
      }
    }
  }
  if(doSync) snes_syncCycles(dma->snes, false, cpuCycles);
}

static void dma_transferByte(Dma* dma, uint16_t aAdr, uint8_t aBank, uint8_t bAdr, bool fromB) {
  // accessing 0x2180 via b-bus while a-bus accesses ram gives open bus
  bool validB = !(bAdr == 0x80 && (aBank == 0x7e || aBank == 0x7f || (
    (aBank < 0x40 || (aBank >= 0x80 && aBank < 0xc0)) && aAdr < 0x2000
  )));
  // accesing b-bus, or dma regs via a-bus gives open bus
  bool validA = !((aBank < 0x40 || (aBank >= 0x80 && aBank < 0xc0)) && (
    aAdr == 0x420b || aAdr == 0x420c || (aAdr >= 0x4300 && aAdr < 0x4380) || (aAdr >= 0x2100 && aAdr < 0x2200)
  ));
  if(fromB) {
    uint8_t val = validB ? snes_readBBus(dma->snes, bAdr) : dma->snes->openBus;
    if(validA) snes_write(dma->snes, (aBank << 16) | aAdr, val);
  } else {
    uint8_t val = validA ? snes_read(dma->snes, (aBank << 16) | aAdr) : dma->snes->openBus;
    if(validB) snes_writeBBus(dma->snes, bAdr, val);
  }
}

void dma_handleDma(Dma* dma, int cpuCycles) {
  // if hdma triggered, do it, except if dmastate indicates dma will be done now
  // (it will be done as part of the dma in that case)
  if(dma->hdmaInitRequested && dma->dmaState != 2) dma_initHdma(dma, true, cpuCycles);
  if(dma->hdmaRunRequested && dma->dmaState != 2) dma_doHdma(dma, true, cpuCycles);
  if(dma->dmaState == 1) {
    dma->dmaState = 2;
    return;
  }
  if(dma->dmaState == 2) {
    // do dma
    dma_doDma(dma, cpuCycles);
    dma->dmaState = 0;
  }
}

void dma_startDma(Dma* dma, uint8_t val, bool hdma) {
  for(int i = 0; i < 8; i++) {
    if(hdma) {
      dma->channel[i].hdmaActive = val & (1 << i);
    } else {
      dma->channel[i].dmaActive = val & (1 << i);
    }
  }
  if(!hdma) {
    dma->dmaState = val != 0 ? 1 : 0;
  }
}
