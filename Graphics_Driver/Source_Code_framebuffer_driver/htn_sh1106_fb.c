// SPDX-License-Identifier: GPL-2.0
/*
 * htn_sh1106_fb.c - Minimal Linux fbdev driver for 1.3" SH1106 OLED
 * 128x64, I2C, 1 bit per pixel.
 *
 * Target: Raspberry Pi / Linux 6.x
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define HTN_DRV_NAME       "htn_sh1106_fb"
#define HTN_WIDTH          128
#define HTN_HEIGHT         64
#define HTN_PAGES          (HTN_HEIGHT / 8)
#define HTN_COL_OFFSET     2
#define HTN_FB_SIZE        (HTN_WIDTH * HTN_HEIGHT / 8)
#define HTN_REFRESH_HZ     20
#define HTN_I2C_CHUNK      16

struct htn_oled {
    struct i2c_client *client;
    struct fb_info *info;
    struct mutex lock;
};

static int htn_i2c_write(struct i2c_client *client, u8 control,
                         const u8 *data, size_t len)
{
    u8 buf[1 + HTN_I2C_CHUNK];
    int ret;

    if (len > HTN_I2C_CHUNK)
        return -EINVAL;

    buf[0] = control;
    memcpy(&buf[1], data, len);

    ret = i2c_master_send(client, buf, len + 1);
    if (ret < 0)
        return ret;
    if (ret != len + 1)
        return -EIO;

    return 0;
}

static int htn_write_cmd(struct htn_oled *oled, u8 cmd)
{
    return htn_i2c_write(oled->client, 0x00, &cmd, 1);
}

static int htn_write_cmd2(struct htn_oled *oled, u8 cmd, u8 arg)
{
    int ret;

    ret = htn_write_cmd(oled, cmd);
    if (ret)
        return ret;

    return htn_write_cmd(oled, arg);
}

static int htn_hw_init(struct htn_oled *oled)
{
    int ret;

#define CMD(c) do { ret = htn_write_cmd(oled, (c)); if (ret) return ret; } while (0)
#define CMD2(c,a) do { ret = htn_write_cmd2(oled, (c), (a)); if (ret) return ret; } while (0)

    CMD(0xAE);          /* display off */
    CMD2(0xD5, 0x80);   /* display clock */
    CMD2(0xA8, 0x3F);   /* multiplex 1/64 */
    CMD2(0xD3, 0x00);   /* display offset */
    CMD(0x40);          /* start line = 0 */

    /* SH1106 DC-DC control */
    CMD2(0xAD, 0x8B);

    CMD(0xA1);          /* segment remap */
    CMD(0xC8);          /* COM scan direction */
    CMD2(0xDA, 0x12);   /* COM pins config */
    CMD2(0x81, 0x80);   /* contrast */
    CMD2(0xD9, 0x22);   /* pre-charge */
    CMD2(0xDB, 0x35);   /* VCOM deselect */
    CMD(0xA4);          /* resume RAM display */
    CMD(0xA6);          /* normal display */
    CMD(0xAF);          /* display on */

#undef CMD
#undef CMD2
    return 0;
}

static int htn_set_page_col(struct htn_oled *oled, u8 page, u8 col)
{
    int ret;
    u8 real_col = col + HTN_COL_OFFSET;

    ret = htn_write_cmd(oled, 0xB0 | (page & 0x0F));
    if (ret)
        return ret;

    ret = htn_write_cmd(oled, 0x00 | (real_col & 0x0F));
    if (ret)
        return ret;

    return htn_write_cmd(oled, 0x10 | ((real_col >> 4) & 0x0F));
}

static int htn_flush(struct htn_oled *oled)
{
    struct fb_info *info = oled->info;
    const u8 *fb = (const u8 *)info->screen_buffer;
    u8 pagebuf[HTN_WIDTH];
    int page, x, bit, ret;
    size_t off;

    mutex_lock(&oled->lock);

    for (page = 0; page < HTN_PAGES; page++) {
        for (x = 0; x < HTN_WIDTH; x++) {
            u8 v = 0;

            for (bit = 0; bit < 8; bit++) {
                int y = page * 8 + bit;
                size_t idx = y * info->fix.line_length + (x >> 3);
                u8 mask = 0x80 >> (x & 7);

                if (fb[idx] & mask)
                    v |= BIT(bit);
            }
            pagebuf[x] = v;
        }

        ret = htn_set_page_col(oled, page, 0);
        if (ret)
            goto out;

        for (off = 0; off < HTN_WIDTH; off += HTN_I2C_CHUNK) {
            size_t n = min_t(size_t, HTN_I2C_CHUNK, HTN_WIDTH - off);
            ret = htn_i2c_write(oled->client, 0x40, &pagebuf[off], n);
            if (ret)
                goto out;
        }
    }

    ret = 0;
out:
    mutex_unlock(&oled->lock);
    return ret;
}

static void htn_damage_range(struct fb_info *info, off_t off, size_t len)
{
    /* Full-screen refresh: OLED is small (1024 bytes). */
    mod_delayed_work(system_wq, &info->deferred_work, 0);
}

static void htn_damage_area(struct fb_info *info, u32 x, u32 y,
                            u32 width, u32 height)
{
    mod_delayed_work(system_wq, &info->deferred_work, 0);
}

FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS(htn,
                                   htn_damage_range,
                                   htn_damage_area)

static int htn_blank(int blank, struct fb_info *info)
{
    struct htn_oled *oled = info->par;

    if (blank == FB_BLANK_UNBLANK)
        return htn_write_cmd(oled, 0xAF);

    return htn_write_cmd(oled, 0xAE);
}

static const struct fb_ops htn_fb_ops = {
    .owner = THIS_MODULE,
    FB_DEFAULT_DEFERRED_OPS(htn),
    .fb_blank = htn_blank,
};

static void htn_deferred_io(struct fb_info *info,
                            struct list_head *pagereflist)
{
    struct htn_oled *oled = info->par;
    int ret = htn_flush(oled);

    if (ret)
        dev_err_ratelimited(&oled->client->dev,
                            "OLED refresh failed: %d\n", ret);
}

static const struct fb_fix_screeninfo htn_fix_template = {
    .id = "HTN-SH1106",
    .type = FB_TYPE_PACKED_PIXELS,
    .visual = FB_VISUAL_MONO10,
    .xpanstep = 0,
    .ypanstep = 0,
    .ywrapstep = 0,
    .accel = FB_ACCEL_NONE,
};

static const struct fb_var_screeninfo htn_var_template = {
    .xres = HTN_WIDTH,
    .yres = HTN_HEIGHT,
    .xres_virtual = HTN_WIDTH,
    .yres_virtual = HTN_HEIGHT,
    .bits_per_pixel = 1,
    .red = { .offset = 0, .length = 1 },
    .green = { .offset = 0, .length = 1 },
    .blue = { .offset = 0, .length = 1 },
    .activate = FB_ACTIVATE_NOW,
    .height = -1,
    .width = -1,
};

static int htn_probe(struct i2c_client *client)
{
    struct fb_info *info;
    struct htn_oled *oled;
    struct fb_deferred_io *defio;
    u8 *vmem;
    int ret;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
        return -EOPNOTSUPP;

    info = framebuffer_alloc(sizeof(*oled), &client->dev);
    if (!info)
        return -ENOMEM;

    oled = info->par;
    oled->client = client;
    oled->info = info;
    mutex_init(&oled->lock);

    vmem = vzalloc(HTN_FB_SIZE);
    if (!vmem) {
        ret = -ENOMEM;
        goto err_fb_release;
    }

    defio = devm_kzalloc(&client->dev, sizeof(*defio), GFP_KERNEL);
    if (!defio) {
        ret = -ENOMEM;
        goto err_vfree;
    }

    defio->delay = max_t(unsigned long, 1, HZ / HTN_REFRESH_HZ);
    defio->deferred_io = htn_deferred_io;

    info->fbops = &htn_fb_ops;
    info->fix = htn_fix_template;
    info->var = htn_var_template;
    info->fix.line_length = DIV_ROUND_UP(HTN_WIDTH, 8);
    info->fix.smem_len = HTN_FB_SIZE;
    info->screen_buffer = vmem;
    info->fbdefio = defio;
    info->flags = FBINFO_VIRTFB;

    ret = fb_deferred_io_init(info);
    if (ret)
        goto err_vfree;

    i2c_set_clientdata(client, info);

    ret = htn_hw_init(oled);
    if (ret) {
        dev_err(&client->dev, "SH1106 init failed: %d\n", ret);
        goto err_defio;
    }

    ret = htn_flush(oled);
    if (ret)
        goto err_display_off;

    ret = register_framebuffer(info);
    if (ret)
        goto err_display_off;

    dev_info(&client->dev,
             "fb%d registered: SH1106 128x64, I2C addr=0x%02x\n",
             info->node, client->addr);
    return 0;

err_display_off:
    htn_write_cmd(oled, 0xAE);
err_defio:
    fb_deferred_io_cleanup(info);
err_vfree:
    vfree(vmem);
err_fb_release:
    framebuffer_release(info);
    return ret;
}

static void htn_remove(struct i2c_client *client)
{
    struct fb_info *info = i2c_get_clientdata(client);
    struct htn_oled *oled;

    if (!info)
        return;

    oled = info->par;
    htn_write_cmd(oled, 0xAE);
    unregister_framebuffer(info);
    fb_deferred_io_cleanup(info);
    vfree(info->screen_buffer);
    framebuffer_release(info);
}

static const struct of_device_id htn_of_match[] = {
    { .compatible = "htn,sh1106-fb" },
    { }
};
MODULE_DEVICE_TABLE(of, htn_of_match);

static const struct i2c_device_id htn_i2c_ids[] = {
    { "htn_sh1106_fb", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, htn_i2c_ids);

static struct i2c_driver htn_i2c_driver = {
    .driver = {
        .name = HTN_DRV_NAME,
        .of_match_table = htn_of_match,
    },
    .probe = htn_probe,
    .remove = htn_remove,
    .id_table = htn_i2c_ids,
};

module_i2c_driver(htn_i2c_driver);

MODULE_AUTHOR("HTN / OpenAI");
MODULE_DESCRIPTION("128x64 SH1106 I2C OLED framebuffer driver");
MODULE_LICENSE("GPL");
