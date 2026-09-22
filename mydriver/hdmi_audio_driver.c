#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Custom HDMI Audio Codec Driver");

// 1. DAI Operations: Xử lý đóng gói InfoFrame và luồng tín hiệu HDMI
static int hdmi_audio_hw_params(struct snd_pcm_substream *substream,
                                struct snd_pcm_hw_params *params,
                                struct snd_soc_dai *dai)
{
    unsigned int rate = params_rate(params);
    unsigned int channels = params_channels(params);

    // Trong phần cứng thật: Tại đây sẽ khởi tạo Audio InfoFrame gửi sang HDMI Display Engine
    pr_info("HDMI Audio: Configured rate=%u Hz, channels=%u (Audio InfoFrame Updated)\n",
            rate, channels);
    return 0;
}

static int hdmi_audio_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
        pr_info("HDMI Audio: Stream STARTED (Sending audio packets via HDMI Data Island)\n");
        break;
    case SNDRV_PCM_TRIGGER_STOP:
        pr_info("HDMI Audio: Stream STOPPED\n");
        break;
    }
    return 0;
}

static const struct snd_soc_dai_ops hdmi_audio_dai_ops = {
    .hw_params = hdmi_audio_hw_params,
    .trigger   = hdmi_audio_trigger,
};

// 2. DAI Driver: Khai báo khả năng HDMI (Hỗ trợ 8 kênh và Passthrough)
static struct snd_soc_dai_driver hdmi_audio_dai = {
    .name = "hdmi-audio-dai",
    .ops = &hdmi_audio_dai_ops,
    .playback = {
        .stream_name = "HDMI Playback",
        .channels_min = 2,
        .channels_max = 8, // HDMI hỗ trợ tới 8 kênh (Vòm 7.1)
        .rates = SNDRV_PCM_RATE_8000_192000, // Đã sửa: Chuẩn Macro dải tần ALSA Kernel hỗ trợ tới 192kHz
        .formats = SNDRV_PCM_FMTBIT_S16_LE |
                   SNDRV_PCM_FMTBIT_S24_LE |
                   SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE, // Format cho phép Passthrough Dolby/DTS
    },
};

// 3. Component Driver cho HDMI
static const struct snd_soc_component_driver hdmi_audio_component = {
    .name = "hdmi-audio-component",
};

// 4. Lifecycle Platform Driver
static int hdmi_audio_probe(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "HDMI Audio Driver: Registering ASoC component...\n");
    return devm_snd_soc_register_component(&pdev->dev, &hdmi_audio_component, &hdmi_audio_dai, 1);
}

static int hdmi_audio_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "HDMI Audio Driver: Unregistered.\n");
    return 0;
}

static struct platform_driver hdmi_audio_driver = {
    .driver = {
        .name = "hdmi_audio_driver",
    },
    .probe = hdmi_audio_probe,
    .remove = hdmi_audio_remove,
};

module_platform_driver(hdmi_audio_driver);