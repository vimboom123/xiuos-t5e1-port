/**
 * @file linkh_t5e1.c
 * @brief 联泓 T5-E1 定制板（板上排针 GND|CEN|RX0|TX0|VBAT）
 *
 * 板级引脚表厂商未提供，原厂 TuyaOS 固件把它存在加密 KV 里。
 * 在确认之前只注册片上音频编解码器，不驱动任何外设脚：
 *   - 喇叭使能脚填 56：tkl_audio.c 只在 spk_gpio < 56 时才去写 GPIO，等于不控 PA
 *   - 不注册 LED / 按键，避免和板上未知线路冲突
 * 查到 PA / 按键脚之后，改下面两个宏即可。
 */

#include "tuya_cloud_types.h"

#include "tal_api.h"

#include "tdd_audio.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define BOARD_SPEAKER_EN_PIN      56   /* 56 = 不控制 */
#define BOARD_SPEAKER_EN_POLARITY TUYA_GPIO_LEVEL_LOW

/***********************************************************
***********************function define**********************
***********************************************************/
static OPERATE_RET __board_register_audio(void)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(AUDIO_CODEC_NAME)
    TDD_AUDIO_T5AI_T cfg = {0};
    memset(&cfg, 0, sizeof(TDD_AUDIO_T5AI_T));

    cfg.aec_enable = 1;

    cfg.ai_chn      = TKL_AI_0;
    cfg.sample_rate = TKL_AUDIO_SAMPLE_16K;
    cfg.data_bits   = TKL_AUDIO_DATABITS_16;
    cfg.channel     = TKL_AUDIO_CHANNEL_MONO;

    cfg.spk_sample_rate  = TKL_AUDIO_SAMPLE_16K;
    cfg.spk_pin          = BOARD_SPEAKER_EN_PIN;
    cfg.spk_pin_polarity = BOARD_SPEAKER_EN_POLARITY;

    TUYA_CALL_ERR_RETURN(tdd_audio_register(AUDIO_CODEC_NAME, cfg));
#endif
    return rt;
}

/**
 * @brief Registers all the hardware peripherals on the board.
 *
 * @return Returns OPRT_OK on success, or an error code on failure.
 */
OPERATE_RET board_register_hardware(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_LOG(__board_register_audio());

    return rt;
}
