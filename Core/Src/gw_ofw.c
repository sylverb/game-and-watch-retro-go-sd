#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "main.h"
#include "gw_ofw.h"

#pragma pack(push, 1)
typedef struct
{
    unsigned int external_flash_size : 24;
    unsigned int must_be_4: 4;
    unsigned int is_mario : 1;
    unsigned int is_zelda : 1;
    unsigned int _padding : 2;  // Padding to align to byte boundary
} Bank1FirmwareMetadata;
#pragma pack(pop)

Bank1FirmwareMetadata bank1_firmware_metadata;
static bool is_loaded = false;
static void load_bank1_firmware_metadata()
{
    if (!is_loaded)
    {
        // Load firmware data from firmware's HDMI-CEC field in vector table.
        // Defaults to Bank 1 (0x08000000 + 0x1B8).
        bank1_firmware_metadata = *(Bank1FirmwareMetadata*)(OFW_ADDRESS + 0x1B8);
        if(bank1_firmware_metadata.must_be_4 != 4){
            // This data came from an uncontrolled source; 0 everything out.
            bank1_firmware_metadata = (Bank1FirmwareMetadata){0};
        }
        is_loaded = true;
    }
}

uint32_t get_ofw_extflash_size()
{
    load_bank1_firmware_metadata();
    // The external_flash_size is stored in 4KB units, so we shift left by 12 bits to convert to bytes.
    return bank1_firmware_metadata.external_flash_size << 12;
}

bool get_ofw_is_present()
{
    load_bank1_firmware_metadata();
    return bank1_firmware_metadata.must_be_4 == 4;
}


bool get_ofw_is_zelda()
{
    load_bank1_firmware_metadata();
    return bank1_firmware_metadata.is_zelda == 1;
}

bool get_ofw_is_mario()
{
    load_bank1_firmware_metadata();
    return bank1_firmware_metadata.is_mario == 1;
}
