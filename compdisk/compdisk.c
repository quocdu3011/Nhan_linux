// SPDX-License-Identifier: GPL-2.0
/*
 * compdisk.c - RAM-backed compressed block device for Linux.
 *
 * Every logical 4 KiB chunk is compressed independently with kernel LZO.
 * BIOs are queued to an ordered workqueue because compression and memory
 * allocation may sleep; the submit_bio callback itself stays non-blocking.
 */

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/lzo.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#define COMPDISK_NAME              "compdisk"
#define COMPDISK_PROC_NAME         "compdisk_stats"
#define COMPDISK_BLOCK_SHIFT       12
#define COMPDISK_BLOCK_SIZE        (1U << COMPDISK_BLOCK_SHIFT)
#define COMPDISK_SECTOR_SHIFT      9
#define COMPDISK_SECTOR_SIZE       (1U << COMPDISK_SECTOR_SHIFT)
#define COMPDISK_DEFAULT_SIZE_MB   64U
#define COMPDISK_MAX_SIZE_MB       4096U
#define COMPDISK_DEFAULT_SAVING    10U

/* blk_alloc_disk(queue_limits, node) was introduced in newer 6.x kernels. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
#define COMPDISK_NEW_QUEUE_LIMITS 1
#endif

struct compdisk_block {
	void *data;
	u32 size;
	bool compressed;
	/* Raw because LZO did not reach min_saving_percent (not an LZO error). */
	bool compression_skipped;
};

struct compdisk_stats {
	u64 written_blocks;
	u64 compressed_blocks;
	u64 raw_blocks;
	u64 incompressible_blocks;
	u64 physical_bytes;
	u64 read_requests;
	u64 write_requests;
	u64 discard_requests;
	u64 compression_failures;
};

struct compdisk_device {
	int major;
	sector_t capacity_sectors;
	u64 capacity_bytes;
	u64 nr_blocks;
	struct gendisk *disk;
	struct workqueue_struct *io_wq;
	struct proc_dir_entry *proc_entry;
	struct compdisk_block *blocks;
	struct compdisk_stats stats;

	/* Ordered workqueue serializes I/O; mutex also gives procfs a snapshot. */
	struct mutex lock;
	unsigned char *raw_scratch;
	unsigned char *compressed_scratch;
	unsigned char *io_scratch;
	void *lzo_workmem;
};

struct compdisk_bio_work {
	struct work_struct work;
	struct bio *bio;
	struct compdisk_device *dev;
};

static unsigned int disk_size_mb = COMPDISK_DEFAULT_SIZE_MB;
module_param(disk_size_mb, uint, 0444);
MODULE_PARM_DESC(disk_size_mb, "Logical disk size in MiB (default 64, max 4096)");

static unsigned int min_saving_percent = COMPDISK_DEFAULT_SAVING;
module_param(min_saving_percent, uint, 0444);
MODULE_PARM_DESC(min_saving_percent,
		 "Minimum percentage saved before keeping compressed data (0..99)");

static struct compdisk_device *compdisk;

static int compdisk_load_block(struct compdisk_device *dev, u64 index,
			       unsigned char *dst)
{
	struct compdisk_block *block = &dev->blocks[index];
	size_t output_len;
	int ret;

	if (!block->data) {
		memset(dst, 0, COMPDISK_BLOCK_SIZE);
		return 0;
	}

	if (!block->compressed) {
		if (unlikely(block->size != COMPDISK_BLOCK_SIZE))
			return -EIO;
		memcpy(dst, block->data, COMPDISK_BLOCK_SIZE);
		return 0;
	}

	output_len = COMPDISK_BLOCK_SIZE;
	ret = lzo1x_decompress_safe(block->data, block->size, dst, &output_len);
	if (unlikely(ret != LZO_E_OK || output_len != COMPDISK_BLOCK_SIZE)) {
		pr_err_ratelimited(COMPDISK_NAME
			": corrupt compressed block %llu (ret=%d, len=%zu)\n",
			(unsigned long long)index, ret, output_len);
		return -EIO;
	}
	return 0;
}

/* Replace one 4 KiB block atomically from the driver's point of view. */
static int compdisk_store_block(struct compdisk_device *dev, u64 index,
				const unsigned char *raw)
{
	struct compdisk_block *block = &dev->blocks[index];
	void *new_data;
	void *old_data;
	size_t compressed_len = lzo1x_worst_compress(COMPDISK_BLOCK_SIZE);
	u32 new_size;
	bool use_compressed = false;
	bool compression_skipped = false;
	bool old_compressed;
	bool old_compression_skipped;
	u32 old_size;
	int ret;

	ret = lzo1x_1_compress(raw, COMPDISK_BLOCK_SIZE,
				 dev->compressed_scratch, &compressed_len,
				 dev->lzo_workmem);
	if (ret == LZO_E_OK && compressed_len < COMPDISK_BLOCK_SIZE &&
	    (COMPDISK_BLOCK_SIZE - compressed_len) * 100ULL >=
		COMPDISK_BLOCK_SIZE * (u64)min_saving_percent) {
		use_compressed = true;
		new_size = compressed_len;
		new_data = kmemdup(dev->compressed_scratch, new_size, GFP_NOIO);
	} else {
		new_size = COMPDISK_BLOCK_SIZE;
		new_data = kmemdup(raw, new_size, GFP_NOIO);
		if (ret == LZO_E_OK)
			compression_skipped = true;
		else
			dev->stats.compression_failures++;
	}
	if (!new_data)
		return -ENOMEM;

	old_data = block->data;
	old_size = block->size;
	old_compressed = block->compressed;
	old_compression_skipped = block->compression_skipped;

	block->data = new_data;
	block->size = new_size;
	block->compressed = use_compressed;
	block->compression_skipped = compression_skipped;

	if (!old_data) {
		dev->stats.written_blocks++;
	} else {
		dev->stats.physical_bytes -= old_size;
		if (old_compressed)
			dev->stats.compressed_blocks--;
		else
			dev->stats.raw_blocks--;
		if (old_compression_skipped)
			dev->stats.incompressible_blocks--;
	}
	dev->stats.physical_bytes += new_size;
	if (use_compressed)
		dev->stats.compressed_blocks++;
	else
		dev->stats.raw_blocks++;
	if (compression_skipped)
		dev->stats.incompressible_blocks++;

	kfree(old_data);
	return 0;
}

static int compdisk_read_bytes(struct compdisk_device *dev, u64 position,
			       unsigned char *dst, size_t length)
{
	while (length) {
		u64 block_index = position >> COMPDISK_BLOCK_SHIFT;
		size_t offset = position & (COMPDISK_BLOCK_SIZE - 1);
		size_t count = min_t(size_t, length,
				     COMPDISK_BLOCK_SIZE - offset);
		int ret;

		ret = compdisk_load_block(dev, block_index, dev->raw_scratch);
		if (ret)
			return ret;
		memcpy(dst, dev->raw_scratch + offset, count);
		dst += count;
		position += count;
		length -= count;
	}
	return 0;
}

static int compdisk_write_bytes(struct compdisk_device *dev, u64 position,
				const unsigned char *src, size_t length)
{
	while (length) {
		u64 block_index = position >> COMPDISK_BLOCK_SHIFT;
		size_t offset = position & (COMPDISK_BLOCK_SIZE - 1);
		size_t count = min_t(size_t, length,
				     COMPDISK_BLOCK_SIZE - offset);
		int ret;

		if (offset || count != COMPDISK_BLOCK_SIZE) {
			ret = compdisk_load_block(dev, block_index,
						  dev->raw_scratch);
			if (ret)
				return ret;
		}
		memcpy(dev->raw_scratch + offset, src, count);
		ret = compdisk_store_block(dev, block_index, dev->raw_scratch);
		if (ret)
			return ret;
		src += count;
		position += count;
		length -= count;
	}
	return 0;
}

static void compdisk_free_block(struct compdisk_device *dev, u64 index)
{
	struct compdisk_block *block = &dev->blocks[index];

	if (!block->data)
		return;
	dev->stats.written_blocks--;
	dev->stats.physical_bytes -= block->size;
	if (block->compressed)
		dev->stats.compressed_blocks--;
	else
		dev->stats.raw_blocks--;
	if (block->compression_skipped)
		dev->stats.incompressible_blocks--;
	kfree(block->data);
	block->data = NULL;
	block->size = 0;
	block->compressed = false;
	block->compression_skipped = false;
}

static int compdisk_zero_or_discard(struct compdisk_device *dev, u64 position,
				    size_t length, bool discard)
{
	memset(dev->io_scratch, 0, COMPDISK_BLOCK_SIZE);
	while (length) {
		u64 block_index = position >> COMPDISK_BLOCK_SHIFT;
		size_t offset = position & (COMPDISK_BLOCK_SIZE - 1);
		size_t count = min_t(size_t, length,
				     COMPDISK_BLOCK_SIZE - offset);
		int ret;

		if (discard && offset == 0 && count == COMPDISK_BLOCK_SIZE) {
			compdisk_free_block(dev, block_index);
		} else {
			ret = compdisk_write_bytes(dev, position,
						   dev->io_scratch, count);
			if (ret)
				return ret;
		}
		position += count;
		length -= count;
	}
	return 0;
}

static int compdisk_transfer_bio(struct compdisk_device *dev, struct bio *bio,
				 bool write)
{
	struct bio_vec bvec;
	struct bvec_iter iter;
	u64 position = (u64)bio->bi_iter.bi_sector << COMPDISK_SECTOR_SHIFT;

	bio_for_each_segment(bvec, bio, iter) {
		size_t done = 0;

		while (done < bvec.bv_len) {
			size_t count = min_t(size_t, COMPDISK_BLOCK_SIZE,
					     bvec.bv_len - done);
			void *mapped;
			int ret;

			/* Never sleep while a highmem page is locally mapped. */
			if (write) {
				mapped = kmap_local_page(bvec.bv_page);
				memcpy(dev->io_scratch,
				       mapped + bvec.bv_offset + done, count);
				kunmap_local(mapped);
				ret = compdisk_write_bytes(dev, position,
							   dev->io_scratch, count);
			} else {
				ret = compdisk_read_bytes(dev, position,
							  dev->io_scratch, count);
				if (ret)
					return ret;
				mapped = kmap_local_page(bvec.bv_page);
				memcpy(mapped + bvec.bv_offset + done,
				       dev->io_scratch, count);
				kunmap_local(mapped);
			}
			if (ret)
				return ret;
			done += count;
			position += count;
		}
	}
	return 0;
}

static void compdisk_bio_worker(struct work_struct *work)
{
	struct compdisk_bio_work *bio_work =
		container_of(work, struct compdisk_bio_work, work);
	struct compdisk_device *dev = bio_work->dev;
	struct bio *bio = bio_work->bio;
	u64 position = (u64)bio->bi_iter.bi_sector << COMPDISK_SECTOR_SHIFT;
	u64 length = bio->bi_iter.bi_size;
	int ret = 0;

	if (unlikely(position > dev->capacity_bytes ||
		     length > dev->capacity_bytes - position)) {
		ret = -EIO;
		goto complete;
	}

	mutex_lock(&dev->lock);
	switch (bio_op(bio)) {
	case REQ_OP_READ:
		dev->stats.read_requests++;
		ret = compdisk_transfer_bio(dev, bio, false);
		break;
	case REQ_OP_WRITE:
		dev->stats.write_requests++;
		ret = compdisk_transfer_bio(dev, bio, true);
		break;
	case REQ_OP_FLUSH:
		/* Ordered workqueue makes every earlier write visible here. */
		break;
	case REQ_OP_DISCARD:
		dev->stats.discard_requests++;
		ret = compdisk_zero_or_discard(dev, position, length, true);
		break;
	case REQ_OP_WRITE_ZEROES:
		dev->stats.write_requests++;
		ret = compdisk_zero_or_discard(dev, position, length, false);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	mutex_unlock(&dev->lock);

complete:
	if (ret)
		bio->bi_status = errno_to_blk_status(ret);
	bio_endio(bio);
	kfree(bio_work);
}

static void compdisk_submit_bio(struct bio *bio)
{
	struct compdisk_device *dev = bio->bi_bdev->bd_disk->private_data;
	struct compdisk_bio_work *bio_work;

	/* submit_bio may run in atomic context; all sleeping work is deferred. */
	bio_work = kmalloc(sizeof(*bio_work), GFP_ATOMIC);
	if (!bio_work) {
		bio->bi_status = BLK_STS_RESOURCE;
		bio_endio(bio);
		return;
	}
	INIT_WORK(&bio_work->work, compdisk_bio_worker);
	bio_work->bio = bio;
	bio_work->dev = dev;
	queue_work(dev->io_wq, &bio_work->work);
}

static const struct block_device_operations compdisk_fops = {
	.owner = THIS_MODULE,
	.submit_bio = compdisk_submit_bio,
};

static int compdisk_stats_show(struct seq_file *m, void *v)
{
	struct compdisk_device *dev = compdisk;
	u64 logical_used;
	u64 physical_used;
	u64 ratio_x100 = 0;
	u64 saved_x10 = 0;

	(void)v;
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->lock);
	logical_used = dev->stats.written_blocks * COMPDISK_BLOCK_SIZE;
	physical_used = dev->stats.physical_bytes;
	if (physical_used)
		ratio_x100 = div64_u64(logical_used * 100, physical_used);
	if (logical_used && logical_used >= physical_used)
		saved_x10 = div64_u64((logical_used - physical_used) * 1000,
					logical_used);

	seq_puts(m, "CompDisk Statistics\n");
	seq_printf(m, "Logical size: %u MB\n", disk_size_mb);
	seq_printf(m, "Block size: %u bytes\n", COMPDISK_BLOCK_SIZE);
	seq_printf(m, "Total blocks: %llu\n",
		   (unsigned long long)dev->nr_blocks);
	seq_printf(m, "Written blocks: %llu\n",
		   (unsigned long long)dev->stats.written_blocks);
	seq_printf(m, "Compressed blocks: %llu\n",
		   (unsigned long long)dev->stats.compressed_blocks);
	seq_printf(m, "Raw blocks: %llu\n",
		   (unsigned long long)dev->stats.raw_blocks);
	seq_printf(m, "Blocks skipped (insufficient saving): %llu\n",
		   (unsigned long long)dev->stats.incompressible_blocks);
	seq_printf(m, "Compression failures: %llu\n",
		   (unsigned long long)dev->stats.compression_failures);
	seq_printf(m, "Minimum saving threshold: %u%%\n",
		   min_saving_percent);
	seq_printf(m, "Logical used: %llu bytes\n",
		   (unsigned long long)logical_used);
	seq_printf(m, "Physical used: %llu bytes\n",
		   (unsigned long long)physical_used);
	seq_printf(m, "Compression ratio: %llu.%02llu\n",
		   (unsigned long long)(ratio_x100 / 100),
		   (unsigned long long)(ratio_x100 % 100));
	seq_printf(m, "Memory saved: %llu.%llu%%\n",
		   (unsigned long long)(saved_x10 / 10),
		   (unsigned long long)(saved_x10 % 10));
	seq_printf(m, "Read requests: %llu\n",
		   (unsigned long long)dev->stats.read_requests);
	seq_printf(m, "Write requests: %llu\n",
		   (unsigned long long)dev->stats.write_requests);
	seq_printf(m, "Discard requests: %llu\n",
		   (unsigned long long)dev->stats.discard_requests);
	mutex_unlock(&dev->lock);
	return 0;
}

static int compdisk_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, compdisk_stats_show, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops compdisk_proc_ops = {
	.proc_open = compdisk_stats_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations compdisk_proc_ops = {
	.owner = THIS_MODULE,
	.open = compdisk_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static void compdisk_free_storage(struct compdisk_device *dev)
{
	u64 i;

	if (dev->blocks) {
		for (i = 0; i < dev->nr_blocks; ++i)
			kfree(dev->blocks[i].data);
	}
	kvfree(dev->blocks);
	kfree(dev->raw_scratch);
	kfree(dev->compressed_scratch);
	kfree(dev->io_scratch);
	kfree(dev->lzo_workmem);
}

static int __init compdisk_init(void)
{
	struct compdisk_device *dev;
	int ret;

	if (!disk_size_mb || disk_size_mb > COMPDISK_MAX_SIZE_MB) {
		pr_err(COMPDISK_NAME ": disk_size_mb must be in range 1..%u\n",
		       COMPDISK_MAX_SIZE_MB);
		return -EINVAL;
	}
	if (min_saving_percent > 99) {
		pr_err(COMPDISK_NAME ": min_saving_percent must be 0..99\n");
		return -EINVAL;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	compdisk = dev;
	mutex_init(&dev->lock);
	dev->capacity_bytes = (u64)disk_size_mb << 20;
	dev->capacity_sectors = dev->capacity_bytes >> COMPDISK_SECTOR_SHIFT;
	dev->nr_blocks = dev->capacity_bytes >> COMPDISK_BLOCK_SHIFT;

	dev->blocks = kvcalloc(dev->nr_blocks, sizeof(*dev->blocks), GFP_KERNEL);
	dev->raw_scratch = kmalloc(COMPDISK_BLOCK_SIZE, GFP_KERNEL);
	dev->compressed_scratch =
		kmalloc(lzo1x_worst_compress(COMPDISK_BLOCK_SIZE), GFP_KERNEL);
	dev->io_scratch = kmalloc(COMPDISK_BLOCK_SIZE, GFP_KERNEL);
	dev->lzo_workmem = kmalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
	if (!dev->blocks || !dev->raw_scratch || !dev->compressed_scratch ||
	    !dev->io_scratch || !dev->lzo_workmem) {
		ret = -ENOMEM;
		goto err_storage;
	}

	dev->io_wq = alloc_ordered_workqueue(COMPDISK_NAME "_io",
					       WQ_MEM_RECLAIM);
	if (!dev->io_wq) {
		ret = -ENOMEM;
		goto err_storage;
	}

	dev->major = register_blkdev(0, COMPDISK_NAME);
	if (dev->major < 0) {
		ret = dev->major;
		goto err_wq;
	}

#ifdef COMPDISK_NEW_QUEUE_LIMITS
	{
		struct queue_limits limits = {
			.logical_block_size = COMPDISK_BLOCK_SIZE,
			.physical_block_size = COMPDISK_BLOCK_SIZE,
			.io_min = COMPDISK_BLOCK_SIZE,
			.max_hw_sectors = 1024,
			.max_discard_sectors = UINT_MAX,
			.discard_granularity = COMPDISK_BLOCK_SIZE,
		};

		dev->disk = blk_alloc_disk(&limits, NUMA_NO_NODE);
	}
#else
	dev->disk = blk_alloc_disk(NUMA_NO_NODE);
#endif
	if (IS_ERR(dev->disk)) {
		ret = PTR_ERR(dev->disk);
		dev->disk = NULL;
		goto err_major;
	}

#ifndef COMPDISK_NEW_QUEUE_LIMITS
	blk_queue_logical_block_size(dev->disk->queue, COMPDISK_BLOCK_SIZE);
	blk_queue_physical_block_size(dev->disk->queue, COMPDISK_BLOCK_SIZE);
	blk_queue_max_hw_sectors(dev->disk->queue, 1024);
	blk_queue_max_discard_sectors(dev->disk->queue, UINT_MAX);
	dev->disk->queue->limits.discard_granularity = COMPDISK_BLOCK_SIZE;
#endif

	dev->disk->major = dev->major;
	dev->disk->first_minor = 0;
	dev->disk->minors = 1;
	dev->disk->fops = &compdisk_fops;
	dev->disk->private_data = dev;
	dev->disk->flags |= GENHD_FL_NO_PART;
	strscpy(dev->disk->disk_name, COMPDISK_NAME, DISK_NAME_LEN);
	set_capacity(dev->disk, dev->capacity_sectors);

	dev->proc_entry = proc_create(COMPDISK_PROC_NAME, 0444, NULL,
				      &compdisk_proc_ops);
	if (!dev->proc_entry) {
		ret = -ENOMEM;
		goto err_disk;
	}

	ret = device_add_disk(NULL, dev->disk, NULL);
	if (ret)
		goto err_proc;

	pr_info(COMPDISK_NAME ": loaded: %u MiB, %llu blocks, major=%d, "
		"minimum saving=%u%%\n", disk_size_mb,
		(unsigned long long)dev->nr_blocks, dev->major,
		min_saving_percent);
	return 0;

err_proc:
	proc_remove(dev->proc_entry);
	dev->proc_entry = NULL;
err_disk:
	put_disk(dev->disk);
	dev->disk = NULL;
err_major:
	unregister_blkdev(dev->major, COMPDISK_NAME);
err_wq:
	destroy_workqueue(dev->io_wq);
err_storage:
	compdisk_free_storage(dev);
	kfree(dev);
	compdisk = NULL;
	return ret;
}

static void __exit compdisk_exit(void)
{
	struct compdisk_device *dev = compdisk;

	if (!dev)
		return;
	if (dev->proc_entry)
		proc_remove(dev->proc_entry);
	if (dev->disk)
		del_gendisk(dev->disk);
	/* Completes all BIOs queued before del_gendisk returned. */
	if (dev->io_wq)
		destroy_workqueue(dev->io_wq);
	if (dev->disk)
		put_disk(dev->disk);
	if (dev->major > 0)
		unregister_blkdev(dev->major, COMPDISK_NAME);
	compdisk_free_storage(dev);
	kfree(dev);
	compdisk = NULL;
	pr_info(COMPDISK_NAME ": unloaded and released all RAM\n");
}

module_init(compdisk_init);
module_exit(compdisk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linux Systems Programming Project");
MODULE_DESCRIPTION("RAM-backed compressed block device using per-block LZO");
MODULE_VERSION("1.0.0");
