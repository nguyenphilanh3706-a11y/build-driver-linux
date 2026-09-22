#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Custom Microphone Capture ASoC Driver");

#define DRIVER_NAME       "mic_audio_driver"
#define DAI_NAME          "mic-audio-dai"
#define COMPONENT_NAME    "mic-audio-component"

/* Thang đo Gain cho Micro: Từ 0dB đến +30dB (Mỗi nấc tăng 1dB) */
static const DECLARE_TLV_DB_SCALE(mic_gain_tlv, 0, 100, 0);

/* Kcontrols dành riêng cho Capture */
static const struct snd_kcontrol_new mic_controls[] = {
    SOC_SINGLE_TLV("Mic Capture Volume", 0, 0, 30, 0, mic_gain_tlv),
    SOC_SINGLE("Mic Switch", 0, 0, 1, 0), // Nút Bật/Tắt Mic (Mute)
};

static int mic_audio_hw_params(struct snd_pcm_substream *substream,
                               struct snd_pcm_hw_params *params,
                               struct snd_soc_dai *dai)
{
    unsigned int rate = params_rate(params);
    unsigned int channels = params_channels(params);

    pr_info("[%s] Capture Configured: rate=%u Hz, channels=%u\n",
            DRIVER_NAME, rate, channels);
    return 0;
}

static const struct snd_soc_dai_ops mic_audio_dai_ops = {
    .hw_params = mic_audio_hw_params,
};

/* Khai báo DAI Driver với luồng CAPTURE */
static struct snd_soc_dai_driver mic_audio_dai = {
    .name = DAI_NAME,
    .ops = &mic_audio_dai_ops,
    .capture = {  // Chuyển sang stream Capture cho Microphone
        .stream_name  = "Mic Capture",
        .channels_min = 1, // Hỗ trợ Mono (1 Mic)
        .channels_max = 2, // Hỗ trợ Stereo (2 Mic)
        .rates        = SNDRV_PCM_RATE_8000_48000, // Tần số thu âm chuẩn 8kHz - 48kHz
        .formats      = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
    },
};

static const struct snd_soc_component_driver mic_audio_component = {
    .name          = COMPONENT_NAME,
    .controls      = mic_controls,
    .num_controls  = ARRAY_SIZE(mic_controls),
};

static int mic_audio_probe(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "[%s] Registering Microphone Capture Component...\n", DRIVER_NAME);
    return devm_snd_soc_register_component(&pdev->dev, &mic_audio_component, &mic_audio_dai, 1);
}

static int mic_audio_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "[%s] Unregistered.\n", DRIVER_NAME);
    return 0;
}

static struct platform_driver mic_audio_driver = {
    .driver = {
        .name = DRIVER_NAME,
    },
    .probe  = mic_audio_probe,
    .remove = mic_audio_remove,
};

module_platform_driver(mic_audio_driver);