// usbblk.c - USB Bulk-Only Transport + block device blk-mq (mac dinh chi doc)
//
// TONG QUAN LUONG HOAT DONG:
//   cam USB -> usbblk_probe -> hoi INQUIRY / READ CAPACITY -> dang ky /dev/usbblk0
//   doc/ghi file -> kernel goi usbblk_queue_rq -> usbblk_rw -> usbblk_bot_cmd -> USB
//   rut USB -> usbblk_disconnect -> go /dev/usbblk0 va giai phong bo nho

/* ===================== 1. THU VIEN ===================== */
#include <linux/module.h>      /* khung module: MODULE_LICENSE, module_param... */
#include <linux/kernel.h>      /* ham co ban cua kernel */
#include <linux/usb.h>         /* API USB: usb_driver, usb_bulk_msg... */
#include <linux/slab.h>        /* kmalloc / kfree (cap phat bo nho kernel) */
#include <linux/delay.h>       /* msleep */
#include <linux/mutex.h>       /* khoa mutex */
#include <linux/blkdev.h>      /* block device: gendisk, request */
#include <linux/blk-mq.h>      /* block layer da hang doi (blk-mq) */
#include <linux/highmem.h>     /* bvec_kmap_local: map trang bo nho de copy */
#include <linux/unaligned.h>   /* put/get_unaligned_be32: doc ghi so big-endian */

/* ===================== 2. HANG SO ===================== */
/* VID/PID cua USB Kingston DataTraveler. Lay bang lenh lsusb.
 * Day la "nhan nhan dien": kernel chi giao USB nao co dung 2 ma nay cho driver. */
#define USBBLK_VID 0x0951       /* Vendor ID  = ma hang (Kingston) */
#define USBBLK_PID 0x1666       /* Product ID = ma san pham */

#define BOT_CBW_SIG  0x43425355         /* chu ky "USBC": mo dau goi lenh (CBW) */
#define BOT_CSW_SIG  0x53425355         /* chu ky "USBS": mo dau goi bien nhan (CSW) */
#define BOT_TIMEOUT  5000               /* thoi gian cho moi lan truyen USB (ms) */
#define BUF_SIZE     512                /* bo dem nho: INQUIRY, sense, capacity */
#define XFER_SIZE    (64 * 1024)        /* toi da 64 KiB moi lenh READ/WRITE(10) */

/* Tham so module: insmod usbblk.ko allow_write=1 de mo khoa ghi.
 * Mac dinh = 0 (chi doc) de bao ve du lieu tren USB.
 * 0444 = moi nguoi chi duoc doc gia tri trong /sys/module/usbblk/parameters */
static bool allow_write;
module_param(allow_write, bool, 0444);
MODULE_PARM_DESC(allow_write, "Allow writes to the device (default: read-only)");

/* ===================== 3. CAU TRUC DU LIEU ===================== */

/* --- Cau truc giao thuc Bulk-Only --- */

/* CBW (Command Block Wrapper) - "phong bi lenh" gui xuong o USB, dung 31 byte.
 * __packed: bat compiler khong chen byte dem, vi giao thuc quy dinh dung 31 byte. */
struct bot_cbw {
	__le32 sig;             /* chu ky "USBC" */
	__le32 tag;             /* so thu tu lenh, o phai tra lai dung so nay trong CSW */
	__le32 data_len;        /* so byte du lieu se truyen o buoc data */
	u8 flags;               /* 0x80 = du lieu di tu o ve Pi (doc), 0x00 = di xuong o (ghi) */
	u8 lun;                 /* so thu tu don vi logic (thuong la 0) */
	u8 cb_len;              /* do dai lenh SCSI ben trong (6 hoac 10 byte) */
	u8 cb[16];              /* lenh SCSI (INQUIRY, READ(10), WRITE(10)...) */
} __packed;

/* CSW (Command Status Wrapper) - "bien nhan" o tra ve sau moi lenh, dung 13 byte */
struct bot_csw {
	__le32 sig;             /* chu ky "USBS" */
	__le32 tag;             /* phai trung tag cua CBW da gui */
	__le32 residue;         /* so byte CHUA truyen duoc (0 la day du) */
	u8 status;              /* 0 = thanh cong, 1 = that bai, 2 = loi giao thuc */
} __packed;

/* "Ho so" cua mot o USB dang duoc driver quan ly */
struct usbblk_dev {
	struct usb_device *udev;        /* thiet bi USB thuc */
	u8  ifnum;                      /* so thu tu interface USB */
	u8  in_addr, out_addr;          /* dia chi endpoint bulk-in (0x81) va bulk-out (0x02) */
	u16 in_size, out_size;          /* kich thuoc goi toi da cua moi endpoint */
	u32 tag;                        /* bo dem tag, tang moi lenh */
	struct mutex lock;              /* chi cho mot lenh USB chay tai mot thoi diem */
	bool gone;                      /* true = USB da bi rut, tu choi moi yeu cau moi */
	struct bot_cbw *cbw;            /* kmalloc: khong dung stack cho DMA cua USB */
	struct bot_csw *csw;
	u8 *buf;                        /* bo dem nho cho INQUIRY, sense, capacity */
	u8 *xfer;                       /* bo dem trung gian ("bounce buffer") cho READ/WRITE */
	u64 sectors;                    /* tong so sector cua o */
	u32 blksz;                      /* kich thuoc moi sector (phai la 512) */
	struct blk_mq_tag_set tag_set;  /* cau hinh hang doi cua block layer */
	struct gendisk *disk;           /* o dia dang ky voi kernel (usbblk0) */
};

/* ===================== 4. BANG NHAN DIEN THIET BI ===================== */
/* Kernel doc bang nay de biet USB nao thuoc ve driver nay.
 * Khop: VID, PID, class Mass Storage (0x08), subclass 0x06 (SCSI), protocol 0x50 (Bulk-Only).
 * Ket qua nay hien thanh "alias usb:v0951p1666..." trong modinfo. */
static const struct usb_device_id usbblk_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(USBBLK_VID, USBBLK_PID,
					USB_CLASS_MASS_STORAGE, 0x06, 0x50) },
	{ }     /* phan tu rong ket thuc bang */
};
MODULE_DEVICE_TABLE(usb, usbblk_table);

/* ===================== 5. PHAN GIAO TIEP USB ===================== */

/* Khoi phuc sau loi: gui lenh Bulk-Only Mass Storage Reset (0xFF),
 * roi xoa trang thai STALL (halt) o ca hai endpoint de o hoat dong lai. */
static void usbblk_reset_recovery(struct usbblk_dev *dev)
{
	struct usb_device *udev = dev->udev;

	usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0xFF,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			0, dev->ifnum, NULL, 0, 1000);
	usb_clear_halt(udev, usb_rcvbulkpipe(udev, dev->in_addr));
	usb_clear_halt(udev, usb_sndbulkpipe(udev, dev->out_addr));
}

/*
 * HAM LOI CUA DRIVER: chay mot lenh SCSI qua giao thuc Bulk-Only, 3 buoc:
 *     CBW (gui lenh) -> (data: truyen du lieu) -> CSW (nhan bien nhan)
 *
 * Tham so: cdb/cdb_len = lenh SCSI; buf/len = bo dem va so byte du lieu;
 *          data_in = true neu du lieu di tu o ve Pi (doc).
 * Tra ve: 0 = ok; -EREMOTEIO = o bao loi lenh (CHECK CONDITION);
 *         so am khac = loi duong truyen USB.
 */
static int usbblk_bot_cmd(struct usbblk_dev *dev, const u8 *cdb, u8 cdb_len,
			  void *buf, u32 len, bool data_in)
{
	struct usb_device *udev = dev->udev;
	unsigned int pipe_out = usb_sndbulkpipe(udev, dev->out_addr);   /* ong gui xuong o */
	unsigned int pipe_in  = usb_rcvbulkpipe(udev, dev->in_addr);    /* ong nhan tu o */
	unsigned int dpipe = data_in ? pipe_in : pipe_out;      /* ong cho buoc data */
	u32 tag = ++dev->tag;           /* moi lenh mot so thu tu moi */
	int actual, ret, i;

	/* Dien "phong bi" CBW */
	memset(dev->cbw, 0, sizeof(*dev->cbw));
	dev->cbw->sig      = cpu_to_le32(BOT_CBW_SIG);  /* "USBC" */
	dev->cbw->tag      = cpu_to_le32(tag);
	dev->cbw->data_len = cpu_to_le32(len);
	dev->cbw->flags    = data_in ? 0x80 : 0x00;
	dev->cbw->cb_len   = cdb_len;
	memcpy(dev->cbw->cb, cdb, cdb_len);                     /* nhet lenh SCSI vao phong bi */

	/* BUOC 1: gui CBW qua bulk-out */
	ret = usb_bulk_msg(udev, pipe_out, dev->cbw, sizeof(*dev->cbw),
			   &actual, BOT_TIMEOUT);
	if (ret || actual != (int)sizeof(*dev->cbw)) {
		ret = ret ? ret : -EIO;
		goto fatal;
	}

	/* BUOC 2: truyen du lieu (bo qua neu len = 0, vi du lenh TEST UNIT READY) */
	if (len) {
		ret = usb_bulk_msg(udev, dpipe, buf, len, &actual,
				   BOT_TIMEOUT);
		if (ret == -EPIPE)
			usb_clear_halt(udev, dpipe);    /* o bao STALL: xoa roi van doc CSW */
		else if (ret)
			goto fatal;
	}

	/* BUOC 3: nhan CSW qua bulk-in (thu lai 1 lan neu bi STALL) */
	for (i = 0; i < 2; i++) {
		ret = usb_bulk_msg(udev, pipe_in, dev->csw, sizeof(*dev->csw),
				   &actual, BOT_TIMEOUT);
		if (ret == -EPIPE) {
			usb_clear_halt(udev, pipe_in);
			continue;
		}
		break;
	}
	if (ret || actual != (int)sizeof(*dev->csw)) {
		ret = ret ? ret : -EIO;
		goto fatal;
	}

	/* Kiem tra bien nhan: dung chu ky "USBS" va dung tag cua lenh vua gui */
	if (le32_to_cpu(dev->csw->sig) != BOT_CSW_SIG ||
	    le32_to_cpu(dev->csw->tag) != tag) {
		ret = -EIO;
		goto fatal;
	}

	/* Doc ket qua trong CSW */
	switch (dev->csw->status) {
	case 0:
		return 0;               /* thanh cong */
	case 1:
		return -EREMOTEIO;      /* o bao loi lenh (can REQUEST SENSE de biet ly do) */
	default:
		ret = -EIO;             /* loi giao thuc */
		goto fatal;
	}

fatal:
	/* Loi duong truyen: ghi log roi reset o (tru khi o da bi rut) */
	dev_err(&udev->dev, "usbblk: transport error %d, resetting\n", ret);
	if (!dev->gone)
		usbblk_reset_recovery(dev);
	return ret;
}

/* Cho o san sang bang TEST UNIT READY (toi da 10 lan, cach nhau 200 ms).
 * Neu o bao loi, gui REQUEST SENSE de xoa trang thai loi cu truoc khi thu lai.
 * Can thiet vi mot so o can vai tram ms sau khi cam moi san sang. */
static int usbblk_wait_ready(struct usbblk_dev *dev)
{
	static const u8 tur[6] = { 0x00, 0, 0, 0, 0, 0 };       /* TEST UNIT READY (0x00) */
	static const u8 rs[6]  = { 0x03, 0, 0, 0, 18, 0 };      /* REQUEST SENSE (0x03), lay 18 byte */
	int i, ret;

	for (i = 0; i < 10; i++) {
		ret = usbblk_bot_cmd(dev, tur, sizeof(tur), NULL, 0, false);
		if (!ret)
			return 0;
		if (ret == -EREMOTEIO)
			usbblk_bot_cmd(dev, rs, sizeof(rs), dev->buf, 18, true);
		msleep(200);
	}
	return -ETIMEDOUT;
}

/* ===================== 6. PHAN GIAO TIEP BLOCK LAYER ===================== */

/* Xu ly mot yeu cau doc hoac ghi tu kernel: doi vi tri/so luong sector
 * thanh lenh READ(10)/WRITE(10), gui xuong o roi chep du lieu. */
static blk_status_t usbblk_rw(struct usbblk_dev *dev, struct request *rq)
{
	static const u8 rs[6] = { 0x03, 0, 0, 0, 18, 0 };       /* REQUEST SENSE */
	bool write = req_op(rq) == REQ_OP_WRITE;        /* ghi hay doc */
	u32 bytes = blk_rq_bytes(rq);                   /* tong so byte yeu cau */
	u32 nblk = bytes >> 9;                          /* so sector = bytes / 512 */
	u64 lba = blk_rq_pos(rq);                       /* sector bat dau (kernel dua san, don vi 512 B) */
	struct req_iterator iter;
	struct bio_vec bvec;
	u8 cdb[10] = { 0 };                             /* lenh SCSI 10 byte */
	u32 off = 0;
	int ret;

	/* Chan yeu cau vuot dung luong o hoac qua 64 KiB */
	if (lba + nblk > dev->sectors || bytes > XFER_SIZE)
		return BLK_STS_IOERR;

	/* GHI: chep du lieu tu cac trang bo nho cua kernel vao bo dem xfer.
	 * Kernel dua du lieu thanh nhieu manh roi rac nen phai chep tung manh. */
	if (write) {
		rq_for_each_segment(bvec, rq, iter) {
			void *p = bvec_kmap_local(&bvec);       /* map trang de truy cap */

			memcpy(dev->xfer + off, p, bvec.bv_len);
			kunmap_local(p);
			off += bvec.bv_len;
		}
	}

	/* Dung lenh SCSI: byte 0 = ma lenh, byte 2-5 = LBA, byte 7-8 = so sector.
	 * SCSI dung big-endian, con Pi la little-endian nen phai dao thu tu byte. */
	cdb[0] = write ? 0x2A : 0x28;           /* WRITE(10) / READ(10) */
	put_unaligned_be32((u32)lba, &cdb[2]);
	put_unaligned_be16((u16)nblk, &cdb[7]);

	/* Gui lenh xuong o (CBW -> data -> CSW) */
	ret = usbblk_bot_cmd(dev, cdb, sizeof(cdb), dev->xfer, bytes, !write);
	if (ret == -EREMOTEIO) {
		/* O bao loi lenh: lay ly do bang REQUEST SENSE roi in ra log */
		if (!usbblk_bot_cmd(dev, rs, sizeof(rs), dev->buf, 18, true))
			dev_err(&dev->udev->dev,
				"usbblk: %s lba %llu: sense key 0x%x asc 0x%02x/0x%02x\n",
				write ? "WRITE" : "READ", lba,
				dev->buf[2] & 0xf, dev->buf[12], dev->buf[13]);
	}
	/* Loi, hoac o truyen thieu byte (residue != 0) deu coi la that bai */
	if (ret || le32_to_cpu(dev->csw->residue))
		return BLK_STS_IOERR;

	/* DOC: chep du lieu tu bo dem xfer tra lai cac trang bo nho cua kernel */
	if (!write) {
		off = 0;
		rq_for_each_segment(bvec, rq, iter) {
			void *p = bvec_kmap_local(&bvec);

			memcpy(p, dev->xfer + off, bvec.bv_len);
			kunmap_local(p);
			off += bvec.bv_len;
		}
	}
	return BLK_STS_OK;
}

/* Kernel goi ham nay moi khi co yeu cau doc/ghi cho /dev/usbblk0 */
static blk_status_t usbblk_queue_rq(struct blk_mq_hw_ctx *hctx,
				    const struct blk_mq_queue_data *bd)
{
	struct usbblk_dev *dev = hctx->queue->queuedata;
	struct request *rq = bd->rq;
	blk_status_t st;

	blk_mq_start_request(rq);       /* bao kernel: bat dau xu ly yeu cau nay */

	mutex_lock(&dev->lock);         /* chi mot lenh USB tai mot thoi diem */
	if (dev->gone) {
		st = BLK_STS_IOERR;     /* USB da rut: bao loi ngay, khong cho timeout */
	} else {
		switch (req_op(rq)) {
		case REQ_OP_READ:
			st = usbblk_rw(dev, rq);
			break;
		case REQ_OP_WRITE:
			/* chi ghi khi nap voi allow_write=1, nguoc lai tu choi */
			st = allow_write ? usbblk_rw(dev, rq) : BLK_STS_IOERR;
			break;
		default:
			st = BLK_STS_NOTSUPP;   /* loai yeu cau khac (vd flush): chua ho tro */
		}
	}
	mutex_unlock(&dev->lock);

	blk_mq_end_request(rq, st);     /* bao kernel: xong, kem ket qua */
	return BLK_STS_OK;
}

/* Dang ky ham xu ly yeu cau voi blk-mq */
static const struct blk_mq_ops usbblk_mq_ops = {
	.queue_rq = usbblk_queue_rq,
};

/* Cac thao tac cua o dia (chi khai bao module so huu) */
static const struct block_device_operations usbblk_fops = {
	.owner = THIS_MODULE,
};

/* ===================== 7. CAM VA RUT USB ===================== */

/* Giai phong toan bo bo nho cua mot o */
static void usbblk_free(struct usbblk_dev *dev)
{
	usb_put_dev(dev->udev);
	kfree(dev->cbw);
	kfree(dev->csw);
	kfree(dev->buf);
	kfree(dev->xfer);
	kfree(dev);
}

/* Kernel goi khi cam USB khop voi usbblk_table.
 * Cac buoc: tim endpoint -> cap phat bo nho -> hoi o -> dang ky o dia. */
static int usbblk_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	static const u8 inquiry[6]  = { 0x12, 0, 0, 0, 36, 0 };   /* INQUIRY: hoi hang/ten san pham */
	static const u8 readcap[10] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; /* READ CAPACITY: hoi dung luong */
	struct usb_endpoint_descriptor *bin, *bout;
	struct queue_limits lim = {             /* gioi han cua hang doi block */
		.logical_block_size  = 512,
		.physical_block_size = 512,
		.max_hw_sectors      = XFER_SIZE >> 9,      /* toi da 128 sector (64 KiB) moi lenh */
	};
	struct usbblk_dev *dev;
	u32 last_lba;
	int ret;

	/* B1: tim 2 endpoint bulk (in va out) cua interface */
	ret = usb_find_common_endpoints(intf->cur_altsetting,
					&bin, &bout, NULL, NULL);
	if (ret) {
		dev_err(&intf->dev, "usbblk: no bulk in/out endpoints\n");
		return ret;
	}

	/* B2: cap phat ho so va cac bo dem (kmalloc de dung duoc cho DMA) */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	mutex_init(&dev->lock);
	dev->cbw  = kmalloc(sizeof(*dev->cbw), GFP_KERNEL);
	dev->csw  = kmalloc(sizeof(*dev->csw), GFP_KERNEL);
	dev->buf  = kmalloc(BUF_SIZE, GFP_KERNEL);
	dev->xfer = kmalloc(XFER_SIZE, GFP_KERNEL);
	dev->udev = usb_get_dev(interface_to_usbdev(intf));
	if (!dev->cbw || !dev->csw || !dev->buf || !dev->xfer) {
		ret = -ENOMEM;
		goto err;
	}

	/* B3: luu thong tin endpoint */
	dev->ifnum    = intf->cur_altsetting->desc.bInterfaceNumber;
	dev->in_addr  = bin->bEndpointAddress;
	dev->out_addr = bout->bEndpointAddress;
	dev->in_size  = usb_endpoint_maxp(bin);
	dev->out_size = usb_endpoint_maxp(bout);

	dev_info(&intf->dev,
		 "usbblk: bound. bulk-in 0x%02x (%u B), bulk-out 0x%02x (%u B)\n",
		 dev->in_addr, dev->in_size, dev->out_addr, dev->out_size);

	/* B4: cho o san sang */
	ret = usbblk_wait_ready(dev);
	if (ret) {
		dev_err(&intf->dev, "usbblk: device not ready (%d)\n", ret);
		goto err;
	}

	/* B5: INQUIRY - lay hang, ten san pham (byte 8-15 = hang, 16-31 = san pham) */
	ret = usbblk_bot_cmd(dev, inquiry, sizeof(inquiry), dev->buf, 36, true);
	if (ret) {
		dev_err(&intf->dev, "usbblk: INQUIRY failed (%d)\n", ret);
		goto err;
	}
	dev_info(&intf->dev,
		 "usbblk: INQUIRY type=%u vendor='%.8s' product='%.16s' rev='%.4s'\n",
		 dev->buf[0] & 0x1f, (char *)dev->buf + 8,
		 (char *)dev->buf + 16, (char *)dev->buf + 32);

	/* B6: READ CAPACITY - 4 byte dau = sector cuoi, 4 byte sau = kich thuoc sector */
	ret = usbblk_bot_cmd(dev, readcap, sizeof(readcap), dev->buf, 8, true);
	if (ret) {
		dev_err(&intf->dev, "usbblk: READ CAPACITY failed (%d)\n", ret);
		goto err;
	}
	last_lba     = get_unaligned_be32(dev->buf);
	dev->blksz   = get_unaligned_be32(dev->buf + 4);
	dev->sectors = (u64)last_lba + 1;       /* tong so sector = sector cuoi + 1 */
	dev_info(&intf->dev,
		 "usbblk: capacity %llu sectors x %u B = %llu MiB\n",
		 dev->sectors, dev->blksz,
		 (dev->sectors * dev->blksz) >> 20);

	/* Driver chi ho tro sector 512 byte */
	if (dev->blksz != 512) {
		dev_err(&intf->dev, "usbblk: only 512-byte sectors supported\n");
		ret = -EINVAL;
		goto err;
	}

	/* B7: dang ky voi block layer */
	dev->tag_set.ops = &usbblk_mq_ops;
	dev->tag_set.nr_hw_queues = 1;          /* 1 hang doi */
	dev->tag_set.queue_depth = 16;          /* toi da 16 yeu cau cho */
	dev->tag_set.numa_node = NUMA_NO_NODE;
	dev->tag_set.flags = BLK_MQ_F_BLOCKING; /* queue_rq duoc phep ngu (vi dung mutex, USB) */
	dev->tag_set.driver_data = dev;
	ret = blk_mq_alloc_tag_set(&dev->tag_set);
	if (ret)
		goto err;

	dev->disk = blk_mq_alloc_disk(&dev->tag_set, &lim, dev);
	if (IS_ERR(dev->disk)) {
		ret = PTR_ERR(dev->disk);
		goto err_tag;
	}

	dev->disk->fops = &usbblk_fops;
	dev->disk->private_data = dev;
	snprintf(dev->disk->disk_name, DISK_NAME_LEN, "usbblk0");   /* ten /dev/usbblk0 */
	set_capacity(dev->disk, dev->sectors);                      /* bao dung luong cho kernel */
	set_disk_ro(dev->disk, !allow_write);                       /* chi doc neu khong bat allow_write */

	/* add_disk: tu day /dev/usbblk0 xuat hien, kernel tu doc MBR va tao usbblk0p1 */
	ret = add_disk(dev->disk);
	if (ret)
		goto err_disk;

	usb_set_intfdata(intf, dev);    /* luu ho so de ham disconnect lay lai */
	dev_info(&intf->dev, "usbblk: /dev/usbblk0 created (%s)\n",
		 allow_write ? "read-write" : "read-only");
	return 0;

/* Don dep nguoc thu tu neu mot buoc o giua that bai */
err_disk:
	put_disk(dev->disk);
err_tag:
	blk_mq_free_tag_set(&dev->tag_set);
err:
	usbblk_free(dev);
	return ret;
}

/* Kernel goi khi rut USB: go o dia va giai phong tai nguyen */
static void usbblk_disconnect(struct usb_interface *intf)
{
	struct usbblk_dev *dev = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	mutex_lock(&dev->lock);
	dev->gone = true;               /* moi yeu cau den sau se that bai ngay */
	mutex_unlock(&dev->lock);

	del_gendisk(dev->disk);         /* go /dev/usbblk0 khoi he thong */
	put_disk(dev->disk);
	blk_mq_free_tag_set(&dev->tag_set);
	usbblk_free(dev);               /* tra lai bo nho */
	dev_info(&intf->dev, "usbblk: disconnected\n");
}

/* ===================== 8. DANG KY DRIVER ===================== */
static struct usb_driver usbblk_driver = {
	.name       = "usbblk",
	.probe      = usbblk_probe,             /* goi khi cam USB khop */
	.disconnect = usbblk_disconnect,        /* goi khi rut USB */
	.id_table   = usbblk_table,             /* bang nhan dien VID/PID */
};

module_usb_driver(usbblk_driver);       /* macro tao ham init/exit dang ky va huy driver */

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB mass storage block driver - step 3 (blk-mq)");