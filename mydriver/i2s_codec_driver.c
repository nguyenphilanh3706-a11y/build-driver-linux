#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/pcm.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("QuocAnh");
MODULE_DESCRIPTION("Custom I2S Audio Codec Driver");

// Các hàm callback xử lý sự kiện âm thanh
static int i2s_codec_hw_params(struct snd_pcm_substream *substream,
                               struct snd_pcm_hw_params *params,
                               struct snd_soc_dai *dai)
{
    pr_info("I2S Codec: Configured rate=%d Hz, channels=%d\n",
            params_rate(params), params_channels(params));
    return 0;
}

static int i2s_codec_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
        pr_info("I2S Codec: Playback/Capture STARTED\n");
        break;
    case SNDRV_PCM_TRIGGER_STOP:
        pr_info("I2S Codec: Playback/Capture STOPPED\n");
        break;
    }
    return 0;
}

// Gắn các hàm callback vào cấu trúc DAI Ops
static const struct snd_soc_dai_ops i2s_codec_dai_ops = {
    .hw_params = i2s_codec_hw_params,
    .trigger   = i2s_codec_trigger,
};

// 1. Khai báo I2S DAI Driver (Cấu hình kênh, tần số mẫu và định dạng PCM)
static struct snd_soc_dai_driver i2s_codec_dai = {
    .name = "i2s-codec-dai",
    .ops = &i2s_codec_dai_ops,
    .playback = {
        .stream_name = "Playback",
        .channels_min = 1,
        .channels_max = 2,
        .rates = SNDRV_PCM_RATE_8000_48000,
        .formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
    },
    .capture = {
        .stream_name = "Capture",
        .channels_min = 1,
        .channels_max = 2,
        .rates = SNDRV_PCM_RATE_8000_48000,
        .formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
    },
};

// 2. Khai báo Component Driver cho Codec
static const struct snd_soc_component_driver i2s_codec_component = {
    .name = "i2s-codec-component",
};

// 3. Hàm Probe đăng ký Component & DAI với ALSA Core
static int i2s_codec_probe(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "I2S Codec Driver: Registering ASoC component...\n");
    return devm_snd_soc_register_component(&pdev->dev, 
                                           &i2s_codec_component, 
                                           &i2s_codec_dai, 1);
}

// 4. Hàm Remove khi gỡ thiết bị
static int i2s_codec_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "I2S Codec Driver: Unregistered.\n");
    return 0;
}

static const struct of_device_id i2s_codec_of_match[] = {
    { .compatible = "custom,i2s-codec", },
    { }
};
MODULE_DEVICE_TABLE(of, i2s_codec_of_match);

static struct platform_driver i2s_codec_driver = {
    .driver = {
        .name = "i2s_codec_driver",
        .of_match_table = i2s_codec_of_match,
    },
    .probe = i2s_codec_probe,
    .remove = i2s_codec_remove,
};

module_platform_driver(i2s_codec_driver);