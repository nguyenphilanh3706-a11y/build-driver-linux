#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Custom Speaker Amplifier ASoC Driver");

#define DRIVER_NAME       "speaker_audio_driver" // Đã đổi lại chuẩn tên DRIVER_NAME
#define DAI_NAME          "speaker-audio-dai"
#define COMPONENT_NAME    "speaker-audio-component"

/* Thang đo âm lượng Speaker: -50dB đến +6dB */
static const DECLARE_TLV_DB_SCALE(spk_vol_tlv, -5000, 100, 0);

/* 1. Kcontrols điều chỉnh âm lượng & Mute loa */
static const struct snd_kcontrol_new speaker_controls[] = {
    SOC_SINGLE_TLV("Speaker Playback Volume", 0, 0, 56, 0, spk_vol_tlv),
    SOC_SINGLE("Speaker Playback Switch", 0, 0, 1, 0),
};

/* 2. DAPM Widgets: Quản lý sơ đồ nguồn & linh kiện */
static const struct snd_soc_dapm_widget speaker_dapm_widgets[] = {
    SND_SOC_DAPM_DAC("DAC", "HiFi Playback", SND_SOC_NOPM, 0, 0),
    SND_SOC_DAPM_PGA("Speaker Amp", SND_SOC_NOPM, 0, 0, NULL, 0),
    SND_SOC_DAPM_SPK("Ext Speaker", NULL),
};

/* 3. DAPM Routes: Định tuyến đường đi của luồng tiếng (DAC -> Amp -> Loa) */
static const struct snd_soc_dapm_route speaker_dapm_routes[] = {
    { "Speaker Amp", NULL, "DAC" },
    { "Ext Speaker", NULL, "Speaker Amp" },
};

static int speaker_hw_params(struct snd_pcm_substream *substream,
                             struct snd_pcm_hw_params *params,
                             struct snd_soc_dai *dai)
{
    pr_info("[%s] Playback Configured: rate=%u Hz, channels=%u\n",
            DRIVER_NAME, params_rate(params), params_channels(params));
    return 0;
}

static const struct snd_soc_dai_ops speaker_dai_ops = {
    .hw_params = speaker_hw_params,
};

static struct snd_soc_dai_driver speaker_audio_dai = {
    .name = DAI_NAME,
    .ops = &speaker_dai_ops,
    .playback = {
        .stream_name  = "HiFi Playback",
        .channels_min = 1,
        .channels_max = 2,
        .rates        = SNDRV_PCM_RATE_8000_192000,
        .formats      = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
    },
};

static const struct snd_soc_component_driver speaker_audio_component = {
    .name                 = COMPONENT_NAME,
    .controls             = speaker_controls,
    .num_controls         = ARRAY_SIZE(speaker_controls),
    .dapm_widgets         = speaker_dapm_widgets,
    .num_dapm_widgets     = ARRAY_SIZE(speaker_dapm_widgets),
    .dapm_routes          = speaker_dapm_routes,
    .num_dapm_routes      = ARRAY_SIZE(speaker_dapm_routes),
};

static int speaker_audio_probe(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "[%s] Registering Speaker Amplifier Component...\n", DRIVER_NAME);
    return devm_snd_soc_register_component(&pdev->dev, &speaker_audio_component, &speaker_audio_dai, 1);
}

static int speaker_audio_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "[%s] Unregistered.\n", DRIVER_NAME);
    return 0;
}

static struct platform_driver speaker_audio_driver = {
    .driver = {
        .name = DRIVER_NAME,
    },
    .probe  = speaker_audio_probe,
    .remove = speaker_audio_remove,
};

module_platform_driver(speaker_audio_driver);