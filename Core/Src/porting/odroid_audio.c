#include "odroid_system.h"
#include "odroid_audio.h"
#include "gw_audio.h"
#include <assert.h>

#include "stm32h7xx_hal.h"

uint8_t audio_level = ODROID_AUDIO_VOLUME_MAX;

// the MD audio frequencies are not thoses values
// they are defined inorder to be synchronized with LCD VSYNC
// doing this ther is no frame drop due to dual buffer (VSYNC MODE)
#define MD_AUDIO_FREQ_NTSC 53267
#define MD_AUDIO_FREQ_PAL 53050   // 1061 ech./frame * 50 Hz (= GWENESIS_AUDIO_FREQ_PAL)

// NOT sync with VSYNC TODO
#define GW_AUDIO_FREQUENCY 32768

// Neo Geo YM2610 ADPCM-A native rate: 8 MHz / 432 = 18518.52 Hz (MCKDIV mode)
#define NEOGEO_AUDIO_FREQ 18518

uint32_t audio_frequency = GW_AUDIO_FREQUENCY;

/* set audio frequency  */
static void set_audio_frequency(uint32_t frequency)
{

    /** reconfig PLL2 and SAI */
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SAI1;

    if (frequency == 12000)
    {

        PeriphClkInitStruct.PLL2.PLL2M = 25;
        PeriphClkInitStruct.PLL2.PLL2N = 96;
        PeriphClkInitStruct.PLL2.PLL2P = 10;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
    }
    else if (frequency == 16000)
    {

        PeriphClkInitStruct.PLL2.PLL2M = 25;
        PeriphClkInitStruct.PLL2.PLL2N = 128;
        PeriphClkInitStruct.PLL2.PLL2P = 10;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
    }
    else if (frequency == 18000)
    {

        PeriphClkInitStruct.PLL2.PLL2M = 25;
        PeriphClkInitStruct.PLL2.PLL2N = 144;
        PeriphClkInitStruct.PLL2.PLL2P = 10;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
    }
    else if (frequency == 22050)
    {

        PeriphClkInitStruct.PLL2.PLL2M = 36;
        PeriphClkInitStruct.PLL2.PLL2N = 254;
        PeriphClkInitStruct.PLL2.PLL2P = 10;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 131;
    }
    else if (frequency == 31200)
    {
//DIVM2=42, DIVN2=419, FRACN2=51 DIVP2=10 => 31176.05845, error=0.07679%
        PeriphClkInitStruct.PLL2.PLL2M = 42;
        PeriphClkInitStruct.PLL2.PLL2N = 419;
        PeriphClkInitStruct.PLL2.PLL2P = 10;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 51;
    }
    else if (frequency == 31400)
    {

        PeriphClkInitStruct.PLL2.PLL2M = 33;
        PeriphClkInitStruct.PLL2.PLL2N = 166;
        PeriphClkInitStruct.PLL2.PLL2P = 5;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 25;
    }
    else if (frequency == 31440)
    {

        PeriphClkInitStruct.PLL2.PLL2M = 33;
        PeriphClkInitStruct.PLL2.PLL2N = 166;
        PeriphClkInitStruct.PLL2.PLL2P = 5;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 27;
    }
    else if (frequency == GW_AUDIO_FREQUENCY)
    {
        /* Reconfigure on the fly PLL2 */
        /* config to get 32768Hz */
        /* The audio clock frequency is derived directly */
        /* SAI mode is MCKDIV mode */

        PeriphClkInitStruct.PLL2.PLL2M = 25;
        PeriphClkInitStruct.PLL2.PLL2N = 196;
        PeriphClkInitStruct.PLL2.PLL2P = 10;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 5000;

    }
    /* config to get custom AUDIO Frequency for GENESIS NTSC */
    /*
    DIVM2=21, DIVN2=124, FRACN2=4566 DIVP2=7 =>
    52958.06677163,-diff=0.00039619Hz -error=0.00000075% DIVM2=21, DIVN2=124,
    FRACN2=4565 DIVP2=7 => 52958.01487099,+diff=0.05150445Hz +error=0.00009726%
    VCO input: 3.05 MHz
    VCO output:379.60 MHz
    */
    /* The audio clock frequency is derived directly */
    /* SAI mode is MCKDIV mode */
    else if (frequency == MD_AUDIO_FREQ_NTSC) {
        PeriphClkInitStruct.PLL2.PLL2M = 21;
        PeriphClkInitStruct.PLL2.PLL2N = 124;
        PeriphClkInitStruct.PLL2.PLL2P = 7;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 5000;

        /* config to get custom AUDIO Frequency foor MEGADRIVE PAL */
    }
    /*
    PAL = 1061 ech./frame * 50 Hz = 53050 Hz
    DIVM2=25, DIVN2=148, FRACN2=4566 DIVP2=7 =>
    53056.20465960 Hz, +diff=6.20465960Hz +error=0.01169587%
    VCO input: 2.56 MHz
    VCO output: 380.31 MHz
    */
    /* The audio clock frequency is derived directly */
    /* SAI mode is MCKDIV mode */
    else if (frequency == MD_AUDIO_FREQ_PAL) {
        PeriphClkInitStruct.PLL2.PLL2M = 25;
        PeriphClkInitStruct.PLL2.PLL2N = 148;
        PeriphClkInitStruct.PLL2.PLL2P = 7;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 4566;

    }
    /*
    Neo Geo: the YM2610's own ADPCM-A rate, 8 MHz / 432 = 18518.5185 Hz
    DIVM2=24, DIVN2=96, FRACN2=0, DIVP2=6, MCKDIV=9 =>
    64 MHz / 24 * 96 / 6 / (9 * 256) = 18518.5185 Hz (exact)
    VCO input: 2.67 MHz
    VCO output: 256.00 MHz
    */
    /* SAI mode is MCKDIV mode */
    else if (frequency == NEOGEO_AUDIO_FREQ) {
        PeriphClkInitStruct.PLL2.PLL2M = 24;
        PeriphClkInitStruct.PLL2.PLL2N = 96;
        PeriphClkInitStruct.PLL2.PLL2P = 6;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
    } else if (frequency == 44100) {
        PeriphClkInitStruct.PLL2.PLL2M = 18;
        PeriphClkInitStruct.PLL2.PLL2N = 127;
        PeriphClkInitStruct.PLL2.PLL2P = 5;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 66;
    } else if (frequency == 63360) {
        PeriphClkInitStruct.PLL2.PLL2M = 37;
        PeriphClkInitStruct.PLL2.PLL2N = 75;
        PeriphClkInitStruct.PLL2.PLL2P = 1;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 149;

        /* config to get 48KHz and multiple */
        /* SAI mode is in standard frequency mode */
    } else {

        PeriphClkInitStruct.PLL2.PLL2M = 25;
        PeriphClkInitStruct.PLL2.PLL2N = 192;
        PeriphClkInitStruct.PLL2.PLL2P = 5;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 5;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 1;
    }

    // keep PLL3 unchanged
    PeriphClkInitStruct.PLL3.PLL3M = 4;
    PeriphClkInitStruct.PLL3.PLL3N = 10; //9;
    PeriphClkInitStruct.PLL3.PLL3P = 2;
    PeriphClkInitStruct.PLL3.PLL3Q = 2;
    PeriphClkInitStruct.PLL3.PLL3R = 32; //24;
    PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_3;
    PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    PeriphClkInitStruct.PLL3.PLL3FRACN = 0;

    PeriphClkInitStruct.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL2;
    PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL2;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /* remove the current configuration */
    HAL_SAI_DeInit(&hsai_BlockA1);

    /* Set Audio sample rate at 32768Hz using MCKDIV mode */
    if (frequency == GW_AUDIO_FREQUENCY)
    {

        hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_MCKDIV;
        hsai_BlockA1.Init.Mckdiv = 6;

        /* config to get 48KHz and other standard values */
        /*
    SAI_AUDIO_FREQUENCY_192K      192000U
    SAI_AUDIO_FREQUENCY_96K        96000U
    SAI_AUDIO_FREQUENCY_48K        48000U
    SAI_AUDIO_FREQUENCY_44K        44100U
    SAI_AUDIO_FREQUENCY_32K        32000U
    SAI_AUDIO_FREQUENCY_22K        22050U
    SAI_AUDIO_FREQUENCY_16K        16000U
    SAI_AUDIO_FREQUENCY_11K        11025U
    SAI_AUDIO_FREQUENCY_8K          8000U
    */
    /* Set Audio sample rate at for Genesis (NTSC 60Hz) or MEgadrive (PAL 50Hz) using MCKDIV mode (also for megadrive)*/
    } else if ( (frequency == MD_AUDIO_FREQ_NTSC) || (frequency == MD_AUDIO_FREQ_PAL) )
    {
        hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_MCKDIV;
        hsai_BlockA1.Init.Mckdiv = 4;
    } else if (frequency == NEOGEO_AUDIO_FREQ)
    {
        hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_MCKDIV;
        hsai_BlockA1.Init.Mckdiv = 9;
    /* Set Audio sample rate at various standard frequencies using AudioFrequency mode */
//    } else if (frequency == 12000) {
    } else {
        /* default value 48KHz */
        hsai_BlockA1.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;

        /* check from the different possible values */
        if ((frequency == SAI_AUDIO_FREQUENCY_192K) ||
            (frequency == SAI_AUDIO_FREQUENCY_96K) ||
            (frequency == 63360) ||
            (frequency == SAI_AUDIO_FREQUENCY_48K) ||
            (frequency == SAI_AUDIO_FREQUENCY_44K) ||
            (frequency == SAI_AUDIO_FREQUENCY_32K) ||
            (frequency == 31400) ||
            (frequency == 31440) ||
            (frequency == 31200) ||
            (frequency == SAI_AUDIO_FREQUENCY_22K) ||
            (frequency == 18000) ||
            (frequency == SAI_AUDIO_FREQUENCY_16K) ||
            (frequency == 12000) ||
            (frequency == SAI_AUDIO_FREQUENCY_11K) ||
            (frequency == SAI_AUDIO_FREQUENCY_8K))
            hsai_BlockA1.Init.AudioFrequency = frequency;
    }

    /* apply the new configuration */
    HAL_SAI_Init(&hsai_BlockA1);

    audio_frequency = frequency;
}

void odroid_audio_init(int sample_rate)
{
    set_audio_frequency(sample_rate);
    audio_level = odroid_settings_Volume_get();
}

void odroid_audio_submit(short* stereoAudioBuffer, int frameCount)
{
}

void odroid_audio_volume_set(int level)
{
    audio_level = level;
    odroid_settings_Volume_set(audio_level);
}

int odroid_audio_volume_get()
{
    return audio_level;
}

int odroid_audio_sample_rate_get()
{
    return audio_frequency;
}