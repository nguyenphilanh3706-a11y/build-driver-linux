#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "smartled"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Legion");
MODULE_DESCRIPTION("Smart LED Driver");

struct smartled_data {
    struct gpio_desc *led_gpio;
    bool led_state;
};

static struct smartled_data *smartled;

static int smartled_open(struct inode *inode, struct file *file)
{
    pr_info("smartled: device opened\n");
    return 0;
}

static int smartled_release(struct inode *inode, struct file *file)
{
    pr_info("smartled: device closed\n");
    return 0;
}

static ssize_t smartled_write(struct file *file, const char __user *buf, size_t count, loff_t *f_pos)
{
    char value;

    if (count == 0)
        return 0;

    if (copy_from_user(&value, buf, 1))
        return -EFAULT;

    if (value == '1') {
        if (smartled && smartled->led_gpio)
            gpiod_set_value(smartled->led_gpio, 1);
        pr_info("smartled: LED ON\n");
    } else if (value == '0') {
        if (smartled && smartled->led_gpio)
            gpiod_set_value(smartled->led_gpio, 0);
        pr_info("smartled: LED OFF\n");
    } else {
        pr_warn("smartled: invalid command '%c'\n", value);
        return -EINVAL;
    }

    return count;
}

static const struct file_operations smartled_fops = {
    .owner = THIS_MODULE,
    .open = smartled_open,
    .release = smartled_release,
    .write = smartled_write,
};

static struct miscdevice smartled_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &smartled_fops,
};

static int smartled_probe(struct platform_device *pdev)
{
    int ret;

    pr_info("smartled: probe()\n");

    smartled = devm_kzalloc(&pdev->dev, sizeof(*smartled), GFP_KERNEL);
    if (!smartled)
        return -ENOMEM;

    smartled->led_gpio = devm_gpiod_get_optional(&pdev->dev, "led", GPIOD_OUT_LOW);
    if (IS_ERR(smartled->led_gpio)) {
        ret = PTR_ERR(smartled->led_gpio);
        dev_err(&pdev->dev, "Cannot get LED GPIO: %d\n", ret);
        return ret;
    }

    ret = misc_register(&smartled_miscdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register misc device\n");
        return ret;
    }

    return 0;
}

static int smartled_remove(struct platform_device *pdev)
{
    misc_deregister(&smartled_miscdev);
    pr_info("smartled: remove()\n");
    return 0;
}

static const struct of_device_id smartled_of_match[] = {
    { .compatible = "smartled" },
    { },
};
MODULE_DEVICE_TABLE(of, smartled_of_match);

static struct platform_driver smartled_driver = {
    .probe = smartled_probe,
    .remove = smartled_remove,
    .driver = {
        .name = "smartled",
        .of_match_table = smartled_of_match,
    },
};

module_platform_driver(smartled_driver);