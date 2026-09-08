//Khai báo thư viện và thông tin ModuleSS
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Legion");
MODULE_DESCRIPTION("Smart LED Driver");
//có nhiệm vụ xử lý khi người dùng (User Space) thực hiện thao tác ghi dữ liệu vào file thiết bị (/dev/smartled).
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
//
static const struct file_operations smartled_fops = {
    .owner = THIS_MODULE,
    .write = smartled_write,
};
//Khối file_operations: 
//Ánh xạ các thao tác (như ghi dữ liệu .write) từ ứng dụng người dùng vào hàm xử lý của driver.
static struct miscdevice smartled_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "smartled",
    .fops = &smartled_fops,
};
//Khối miscdevice: 
//Cấu hình tên file thiết bị là "smartled" và liên kết với bảng thao tác file_operations.
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
//Khối smartled_init: 
//Hàm khởi chạy khi nạp module (insmod), đăng ký thiết bị để tự động tạo node /dev/smartled.
static void __exit smartled_exit(void)
{
    misc_deregister(&smartled_misc_device);
    printk(KERN_INFO "SmartLED: Driver unloaded\n");
}
//Khối smartled_exit và Macro: 
//Hàm dọn dẹp khi gỡ module (rmmod) để thu hồi tài nguyên, kết hợp các macro định nghĩa điểm đầu/cuối cho kernel.
module_init(smartled_init);
module_exit(smartled_exit);