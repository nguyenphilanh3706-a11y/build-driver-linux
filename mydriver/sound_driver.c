#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <sound/soc.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ASoC Sound Component Driver");

// 1. Khai báo bảng tương thích với Device Tree (DTS)
static const struct of_device_id sound_driver_of_match[] = {
    { .compatible = "custom,my-sound-driver", },
    { }
};
MODULE_DEVICE_TABLE(of, sound_driver_of_match);

// 2. Cấu hình Component Driver cho ALSA
static const struct snd_soc_component_driver my_sound_component = {
    .name = "my-sound-component",
};

// 3. Hàm Probe: Chạy khi Kernel phát hiện thiết bị phù hợp trong DTS
static int sound_driver_probe(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "Sound Driver: Probing audio device...\n");
    return devm_snd_soc_register_component(&pdev->dev, &my_sound_component, NULL, 0);
}

// 4. Hàm Remove: Chạy khi gỡ thiết bị
static int sound_driver_remove(struct platform_device *pdev)
{
    dev_info(&pdev->dev, "Sound Driver: Audio device removed.\n");
    return 0;
}

// 5. Đăng ký Platform Driver với Kernel
static struct platform_driver sound_platform_driver = {
    .driver = {
        .name = "my_sound_driver",
        .of_match_table = sound_driver_of_match,
    },
    .probe = sound_driver_probe,
    .remove = sound_driver_remove,
};

module_platform_driver(sound_platform_driver);