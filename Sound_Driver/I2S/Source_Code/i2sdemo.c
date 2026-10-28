// i2sdemo.c - ASoC codec + machine driver for a MAX98357A on the Pi I2S port
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

/* ======================= codec driver ======================= */

static int i2sdemo_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	dev_info(dai->dev, "i2sdemo: hw_params rate=%u channels=%u width=%u bits\n",
		 params_rate(params), params_channels(params),
		 params_width(params));
	return 0;
}

static int i2sdemo_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		dev_info(dai->dev, "i2sdemo: playback START\n");
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dev_info(dai->dev, "i2sdemo: playback STOP\n");
		break;
	}
	return 0;
}

static const struct snd_soc_dai_ops i2sdemo_dai_ops = {
	.hw_params = i2sdemo_hw_params,
	.trigger   = i2sdemo_trigger,
};

/* MAX98357A: playback only, 8-96 kHz, 16/24/32-bit I2S */
static struct snd_soc_dai_driver i2sdemo_dai = {
	.name = "i2sdemo-hifi",
	.playback = {
		.stream_name  = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates        = SNDRV_PCM_RATE_8000_96000,
		.formats      = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &i2sdemo_dai_ops,
};

static const struct snd_soc_dapm_widget i2sdemo_widgets[] = {
	SND_SOC_DAPM_OUTPUT("Speaker"),
};

static const struct snd_soc_dapm_route i2sdemo_routes[] = {
	{ "Speaker", NULL, "HiFi Playback" },
};

static const struct snd_soc_component_driver i2sdemo_component = {
	.dapm_widgets     = i2sdemo_widgets,
	.num_dapm_widgets = ARRAY_SIZE(i2sdemo_widgets),
	.dapm_routes      = i2sdemo_routes,
	.num_dapm_routes  = ARRAY_SIZE(i2sdemo_routes),
};

static int i2sdemo_codec_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev, &i2sdemo_component,
					       &i2sdemo_dai, 1);
}

static const struct of_device_id i2sdemo_codec_of_match[] = {
	{ .compatible = "student,i2sdemo-codec" },
	{ }
};
MODULE_DEVICE_TABLE(of, i2sdemo_codec_of_match);

static struct platform_driver i2sdemo_codec_driver = {
	.driver = {
		.name = "i2sdemo-codec",
		.of_match_table = i2sdemo_codec_of_match,
	},
	.probe = i2sdemo_codec_probe,
};

/* ======================= machine driver (card) ======================= */

struct i2sdemo_card {
	struct snd_soc_card card;
	struct snd_soc_dai_link link;
	struct snd_soc_dai_link_component cpu, codec, platform;
	struct device_node *cpu_np, *codec_np;
};

static void i2sdemo_put_nodes(void *data)
{
	struct i2sdemo_card *priv = data;

	of_node_put(priv->cpu_np);
	of_node_put(priv->codec_np);
}

static int i2sdemo_card_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct i2sdemo_card *priv;
	const char *name = "i2sdemo";
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	of_property_read_string(dev->of_node, "card-name", &name);
	priv->cpu_np   = of_parse_phandle(dev->of_node, "i2s-controller", 0);
	priv->codec_np = of_parse_phandle(dev->of_node, "audio-codec", 0);

	ret = devm_add_action_or_reset(dev, i2sdemo_put_nodes, priv);
	if (ret)
		return ret;
	if (!priv->cpu_np || !priv->codec_np)
		return dev_err_probe(dev, -EINVAL,
				     "missing i2s-controller or audio-codec\n");

	/* CPU side: bind by node, kernel picks the first DAI of the I2S block */
	priv->cpu.of_node      = priv->cpu_np;

	/* Codec side: kernel REQUIRES the DAI name (set in i2sdemo_dai.name) */
	priv->codec.of_node    = priv->codec_np;
	priv->codec.dai_name   = "i2sdemo-hifi";

	/* DMA lives in the I2S node */
	priv->platform.of_node = priv->cpu_np;

	priv->link.name          = "i2sdemo-link";
	priv->link.stream_name   = "i2sdemo playback";
	priv->link.cpus          = &priv->cpu;
	priv->link.num_cpus      = 1;
	priv->link.codecs        = &priv->codec;
	priv->link.num_codecs    = 1;
	priv->link.platforms     = &priv->platform;
	priv->link.num_platforms = 1;
	/* I2S, normal clocks, Pi generates BCLK/LRCLK, amp is the consumer */
	priv->link.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			     SND_SOC_DAIFMT_CBC_CFC;

	priv->card.name      = name;
	priv->card.owner     = THIS_MODULE;
	priv->card.dev       = dev;
	priv->card.dai_link  = &priv->link;
	priv->card.num_links = 1;

	ret = devm_snd_soc_register_card(dev, &priv->card);
	if (ret)
		return dev_err_probe(dev, ret, "card registration failed\n");

	dev_info(dev, "i2sdemo: card '%s' registered\n", name);
	return 0;
}

static const struct of_device_id i2sdemo_card_of_match[] = {
	{ .compatible = "student,i2sdemo-card" },
	{ }
};
MODULE_DEVICE_TABLE(of, i2sdemo_card_of_match);

static struct platform_driver i2sdemo_card_driver = {
	.driver = {
		.name = "i2sdemo-card",
		.of_match_table = i2sdemo_card_of_match,
	},
	.probe = i2sdemo_card_probe,
};

/* ======================= module glue ======================= */

static struct platform_driver * const i2sdemo_drivers[] = {
	&i2sdemo_codec_driver,
	&i2sdemo_card_driver,
};

static int __init i2sdemo_init(void)
{
	return platform_register_drivers(i2sdemo_drivers,
					 ARRAY_SIZE(i2sdemo_drivers));
}

static void __exit i2sdemo_exit(void)
{
	platform_unregister_drivers(i2sdemo_drivers,
				    ARRAY_SIZE(i2sdemo_drivers));
}

module_init(i2sdemo_init);
module_exit(i2sdemo_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MAX98357A I2S codec + machine driver (student project)");