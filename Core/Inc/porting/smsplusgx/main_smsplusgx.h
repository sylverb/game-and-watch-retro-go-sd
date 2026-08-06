#pragma once

#define SMSPLUSGX_ENGINE_SG1000  2
#define SMSPLUSGX_ENGINE_COLECO  1
#define SMSPLUSGX_ENGINE_OTHERS  0

/* Dynamic-core entry: engine (SMS/GG vs Coleco vs SG-1000) is selected from
 * ACTIVE_FILE->ext inside the core (col → Coleco, sg → SG-1000, else SMS/GG). */
int app_main_smsplusgx(uint8_t load_state, uint8_t start_paused, int8_t save_slot);
