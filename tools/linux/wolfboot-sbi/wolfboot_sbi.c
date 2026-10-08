// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * wolfboot-sbi: Linux front end for the wolfBoot SBI vendor extension on
 * PolarFire SoC.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * /dev/wolfboot (root only) exposes what the M-mode wolfBoot serves on the
 * Microchip vendor extension: the firmware version, the device serial
 * number, PUF-derived keys by purpose, and the System Controller digest,
 * IAP and auto-update services when the hardware mailbox is fenced off
 * from S-mode.  Nothing here touches the mailbox registers, so the
 * mainline System Controller drivers keep working in an unfenced build.
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/sbi.h>

#include "wolfboot_sbi.h"

#define SBI_EXT_VENDOR_MCHP      0x09000029UL

#define SBI_MCHP_FID_HSS_REBOOT  0x10UL
#define SBI_WOLF_FID_GET_VERSION      0x40UL
#define SBI_WOLF_FID_GET_SERIAL       0x41UL
#define SBI_WOLF_FID_GET_KEY          0x42UL
#define SBI_WOLF_FID_DIGEST_CHECK     0x43UL
#define SBI_WOLF_FID_IAP_AUTHENTICATE 0x44UL
#define SBI_WOLF_FID_IAP_VERIFY       0x45UL
#define SBI_WOLF_FID_IAP_PROGRAM      0x46UL
#define SBI_WOLF_FID_AUTO_UPDATE      0x47UL
#define SBI_WOLF_FID_SC_FLASH_ERASE   0x48UL
#define SBI_WOLF_FID_SC_FLASH_WRITE   0x49UL
#define SBI_WOLF_FID_SC_FLASH_READ    0x4AUL
#define SBI_WOLF_FID_SC_FLASH_SET_IMAGE 0x4BUL
#define SBI_WOLF_FID_SC_STATUS        0x4FUL

#define SBI_WOLF_VERSION_MIN     1UL

/* asm/sbi.h has these from 6.9 on */
#ifndef SBI_ERR_DENIED
#define SBI_ERR_DENIED          (-4)
#endif
#ifndef SBI_ERR_INVALID_ADDRESS
#define SBI_ERR_INVALID_ADDRESS (-5)
#endif
#ifndef SBI_ERR_ALREADY_STARTED
#define SBI_ERR_ALREADY_STARTED (-7)
#endif

static DEFINE_MUTEX(wolfboot_sbi_lock);

static int wolfboot_sbi_errno(long sbi_err)
{
	switch (sbi_err) {
	case SBI_SUCCESS:
		return 0;
	case SBI_ERR_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case SBI_ERR_INVALID_PARAM:
		return -EINVAL;
	case SBI_ERR_DENIED:
		return -EPERM;
	case SBI_ERR_INVALID_ADDRESS:
		return -EFAULT;
	case SBI_ERR_ALREADY_STARTED:
		return -EBUSY;
	default:
		return -EIO;
	}
}

static long wolfboot_sbi_call(unsigned long fid, unsigned long a0,
			      unsigned long a1, unsigned long a2,
			      unsigned long *value)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_VENDOR_MCHP, fid, a0, a1, a2, 0, 0, 0);
	if (value)
		*value = ret.value;
	return ret.error;
}

static long wolfboot_sbi_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	void __user *uarg = (void __user *)arg;
	unsigned long value = 0;
	long err;
	int rc;

	if (mutex_lock_interruptible(&wolfboot_sbi_lock))
		return -ERESTARTSYS;

	switch (cmd) {
	case WOLFBOOT_SBI_IOC_GET_VERSION: {
		u32 v;

		err = wolfboot_sbi_call(SBI_WOLF_FID_GET_VERSION, 0, 0, 0,
					&value);
		rc = wolfboot_sbi_errno(err);
		v = (u32)value;
		if (!rc && copy_to_user(uarg, &v, sizeof(v)))
			rc = -EFAULT;
		break;
	}
	case WOLFBOOT_SBI_IOC_GET_SERIAL: {
		u8 *serial = kzalloc(WOLFBOOT_SBI_SERIAL_LEN, GFP_KERNEL);

		if (!serial) {
			rc = -ENOMEM;
			break;
		}
		/* M-mode writes the buffer through this hart's translation. */
		err = wolfboot_sbi_call(SBI_WOLF_FID_GET_SERIAL,
					(unsigned long)serial,
					WOLFBOOT_SBI_SERIAL_LEN, 0, NULL);
		rc = wolfboot_sbi_errno(err);
		if (!rc && copy_to_user(uarg, serial, WOLFBOOT_SBI_SERIAL_LEN))
			rc = -EFAULT;
		kfree(serial);
		break;
	}
	case WOLFBOOT_SBI_IOC_GET_KEY: {
		struct wolfboot_sbi_key *req;

		req = kzalloc(sizeof(*req), GFP_KERNEL);
		if (!req) {
			rc = -ENOMEM;
			break;
		}
		if (copy_from_user(req, uarg, sizeof(*req))) {
			rc = -EFAULT;
		} else {
			err = wolfboot_sbi_call(SBI_WOLF_FID_GET_KEY,
						req->purpose,
						(unsigned long)req->key,
						WOLFBOOT_SBI_KEY_LEN, NULL);
			rc = wolfboot_sbi_errno(err);
			if (!rc && copy_to_user(uarg, req, sizeof(*req)))
				rc = -EFAULT;
		}
		kfree_sensitive(req);
		break;
	}
	case WOLFBOOT_SBI_IOC_DIGEST_CHECK:
	case WOLFBOOT_SBI_IOC_IAP_AUTH:
	case WOLFBOOT_SBI_IOC_IAP_VERIFY:
	case WOLFBOOT_SBI_IOC_IAP_PROGRAM: {
		unsigned long fid;
		u32 a0;

		if (copy_from_user(&a0, uarg, sizeof(a0))) {
			rc = -EFAULT;
			break;
		}
		switch (cmd) {
		case WOLFBOOT_SBI_IOC_DIGEST_CHECK:
			fid = SBI_WOLF_FID_DIGEST_CHECK;
			break;
		case WOLFBOOT_SBI_IOC_IAP_AUTH:
			fid = SBI_WOLF_FID_IAP_AUTHENTICATE;
			break;
		case WOLFBOOT_SBI_IOC_IAP_VERIFY:
			fid = SBI_WOLF_FID_IAP_VERIFY;
			break;
		default:
			/* The device restarts inside the call on success; the
			 * caller syncs its filesystems first. */
			fid = SBI_WOLF_FID_IAP_PROGRAM;
			break;
		}
		err = wolfboot_sbi_call(fid, a0, 0, 0, NULL);
		rc = wolfboot_sbi_errno(err);
		break;
	}
	case WOLFBOOT_SBI_IOC_AUTO_UPDATE:
		err = wolfboot_sbi_call(SBI_WOLF_FID_AUTO_UPDATE, 0, 0, 0, NULL);
		rc = wolfboot_sbi_errno(err);
		break;
	case WOLFBOOT_SBI_IOC_SC_FLASH_ERASE: {
		struct wolfboot_sbi_flash_range r;
		u32 off;

		if (copy_from_user(&r, uarg, sizeof(r))) {
			rc = -EFAULT;
			break;
		}
		if (!r.len || (r.offset % WOLFBOOT_SBI_FLASH_ERASE_BLOCK) ||
		    (r.len % WOLFBOOT_SBI_FLASH_ERASE_BLOCK) ||
		    r.offset > U32_MAX - r.len) {
			rc = -EINVAL;
			break;
		}
		rc = 0;
		/* one erase block per ecall: each holds its hart for the erase */
		for (off = r.offset; off < r.offset + r.len && !rc;
		     off += WOLFBOOT_SBI_FLASH_ERASE_BLOCK) {
			err = wolfboot_sbi_call(SBI_WOLF_FID_SC_FLASH_ERASE, off,
						WOLFBOOT_SBI_FLASH_ERASE_BLOCK, 0,
						NULL);
			rc = wolfboot_sbi_errno(err);
		}
		break;
	}
	case WOLFBOOT_SBI_IOC_SC_FLASH_WRITE:
	case WOLFBOOT_SBI_IOC_SC_FLASH_READ: {
		struct wolfboot_sbi_flash_io io;
		u8 __user *ubuf;
		u8 *bounce;
		u32 done = 0;

		if (copy_from_user(&io, uarg, sizeof(io))) {
			rc = -EFAULT;
			break;
		}
		if (!io.len || io.offset > U32_MAX - io.len) {
			rc = -EINVAL;
			break;
		}
		ubuf = (u8 __user *)(uintptr_t)io.buf;
		bounce = kzalloc(WOLFBOOT_SBI_FLASH_CHUNK, GFP_KERNEL);
		if (!bounce) {
			rc = -ENOMEM;
			break;
		}
		rc = 0;
		while (done < io.len && !rc) {
			u32 n = min_t(u32, io.len - done, WOLFBOOT_SBI_FLASH_CHUNK);

			if (cmd == WOLFBOOT_SBI_IOC_SC_FLASH_WRITE) {
				if (copy_from_user(bounce, ubuf + done, n)) {
					rc = -EFAULT;
					break;
				}
				err = wolfboot_sbi_call(SBI_WOLF_FID_SC_FLASH_WRITE,
							io.offset + done,
							(unsigned long)bounce, n,
							NULL);
				rc = wolfboot_sbi_errno(err);
			} else {
				err = wolfboot_sbi_call(SBI_WOLF_FID_SC_FLASH_READ,
							io.offset + done,
							(unsigned long)bounce, n,
							NULL);
				rc = wolfboot_sbi_errno(err);
				if (!rc && copy_to_user(ubuf + done, bounce, n))
					rc = -EFAULT;
			}
			done += n;
		}
		kfree(bounce);
		break;
	}
	case WOLFBOOT_SBI_IOC_SC_FLASH_SET_IMAGE: {
		struct wolfboot_sbi_flash_image im;

		if (copy_from_user(&im, uarg, sizeof(im))) {
			rc = -EFAULT;
			break;
		}
		err = wolfboot_sbi_call(SBI_WOLF_FID_SC_FLASH_SET_IMAGE, im.index,
					im.address, 0, NULL);
		rc = wolfboot_sbi_errno(err);
		break;
	}
	case WOLFBOOT_SBI_IOC_SC_STATUS: {
		struct wolfboot_sbi_sc_status st;

		err = wolfboot_sbi_call(SBI_WOLF_FID_SC_STATUS, 0, 0, 0, &value);
		rc = wolfboot_sbi_errno(err);
		st.status = (u32)(value & 0xFFFFUL);
		st.mbox1 = (u32)(value >> 32);
		if (!rc && copy_to_user(uarg, &st, sizeof(st)))
			rc = -EFAULT;
		break;
	}
	default:
		rc = -ENOTTY;
		break;
	}

	mutex_unlock(&wolfboot_sbi_lock);
	return rc;
}

static const struct file_operations wolfboot_sbi_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = wolfboot_sbi_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static struct miscdevice wolfboot_sbi_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "wolfboot",
	.fops = &wolfboot_sbi_fops,
	.mode = 0600,
};

static int __init wolfboot_sbi_init(void)
{
	unsigned long value = 0;
	long err;

	if (sbi_probe_extension(SBI_EXT_VENDOR_MCHP) <= 0)
		return -ENODEV;
	/* The HSS answers the same extension with NOT_SUPPORTED here. */
	err = wolfboot_sbi_call(SBI_WOLF_FID_GET_VERSION, 0, 0, 0, &value);
	if (err != SBI_SUCCESS || value < SBI_WOLF_VERSION_MIN)
		return -ENODEV;

	return misc_register(&wolfboot_sbi_dev);
}

static void __exit wolfboot_sbi_exit(void)
{
	misc_deregister(&wolfboot_sbi_dev);
}

module_init(wolfboot_sbi_init);
module_exit(wolfboot_sbi_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("wolfSSL Inc.");
MODULE_DESCRIPTION("wolfBoot SBI vendor extension front end for PolarFire SoC");
