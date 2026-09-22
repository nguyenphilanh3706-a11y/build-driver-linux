#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

MODULE_LICENSE("GPL");

static void fake_dev_release(struct device *dev) {}

static struct platform_device fake_sound_dev = {
    .name = "i2s_codec_driver", // Trùng tên .name trong sound_platform_driver
    .id = -1,
    .dev = {
        .release = fake_dev_release,
    },
};

static int __init fake_dev_init(void) {
    return platform_device_register(&fake_sound_dev);
}

static void __exit fake_dev_exit(void) {
    platform_device_unregister(&fake_sound_dev);
}

module_init(fake_dev_init);
module_exit(fake_dev_exit);