#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Legion");
MODULE_DESCRIPTION("Smart LED Driver");

static ssize_t smartled_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char kbuf;
    if (copy_from_user(&kbuf, buf, 1))
        return -EFAULT;

    if (kbuf == '1') {
        printk(KERN_INFO "SmartLED: LED ON\n");
    } else if (kbuf == '0') {
        printk(KERN_INFO "SmartLED: LED OFF\n");
    }
    return count;
}

static const struct file_operations smartled_fops = {
    .owner = THIS_MODULE,
    .write = smartled_write,
};

static struct miscdevice smartled_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "smartled",
    .fops = &smartled_fops,
};

static int __init smartled_init(void)
{
    int ret;
    ret = misc_register(&smartled_misc_device);
    if (ret) {
        printk(KERN_ERR "SmartLED: Failed to register misc device\n");
        return ret;
    }
    printk(KERN_INFO "SmartLED: Driver loaded, /dev/smartled created\n");
    return 0;
}

static void __exit smartled_exit(void)
{
    misc_deregister(&smartled_misc_device);
    printk(KERN_INFO "SmartLED: Driver unloaded\n");
}

module_init(smartled_init);
module_exit(smartled_exit);