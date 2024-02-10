#include <linux/module.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/genhd.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("minneelyyyy");
MODULE_DESCRIPTION("An in memory block device");

#define RAMDISK_NAME	"ramdisk"

static int g_major = 0;

static struct ramdisk {
	struct blk_mq_tag_set tag_set;
	struct gendisk *gd;
	struct request_queue *queue;
	unsigned long block_cnt;
	void *block;
} dev;

/***************************
 * BLOCK DEVICE OPERATIONS *
 ***************************/

static struct block_device_operations ramdisk_ops = {
	.owner = THIS_MODULE,
};

/******************
 * QUEUE HANDLING *
 ******************/

static blk_status_t ramdisk_block_request(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	return BLK_STS_OK;
}

static struct blk_mq_ops ramdisk_queue_ops = {
	.queue_rq = ramdisk_block_request
};

/***********
 * TAG SET *
 ***********/

static int init_tag_set(struct blk_mq_tag_set *set, void *driver_data)
{
	set->ops = &ramdisk_queue_ops;
	set->nr_hw_queues = 1;
	set->nr_maps = 1;
	set->queue_depth = 128;
	set->numa_node = NUMA_NO_NODE;
	set->flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_STACKING;
	set->cmd_size = 0;
	set->driver_data = driver_data;

	return blk_mq_alloc_tag_set(set);
}

/************
 * GEN DISK *
 ************/

static int init_gendisk(struct ramdisk *rd)
{
	rd->gd = blk_mq_alloc_disk(&rd->tag_set, rd);

	if (IS_ERR(rd->gd)) {
		printk(KERN_ERR "failed to allocate disk.\n");
		return PTR_ERR(rd->gd);
	}
	
	rd->queue = rd->gd->queue;
	rd->queue->queuedata = rd;

	return 0;
}

static int register_gendisk(struct ramdisk *rd)
{
	struct gendisk *disk = rd->gd;

	set_capacity(disk, rd->block_cnt);

	disk->major = 0;
	disk->first_minor = 0;
	disk->minors = 0;

	disk->fops = &ramdisk_ops;

	disk->private_data = rd;
	strncpy(disk->disk_name, RAMDISK_NAME, DISK_NAME_LEN);

	return add_disk(disk);
}

/************
 * RAM DISK *
 ************/

static int init_ramdisk(struct ramdisk *rd)
{
	int status;

	/* alloc block */
	rd->block_cnt = 4096;
	rd->block = vmalloc(512 * rd->block_cnt);

	if (!rd->block)
		return -ENOMEM;

	status = init_tag_set(&rd->tag_set, rd);

	if (status < 0) {
		printk(KERN_ERR "failed to initialize tag set.\n");
		goto err_init_tag_set;
	}

	status = init_gendisk(rd);

	if (status < 0) {
		printk(KERN_ERR "failed to initialize disk.\n");
		goto err_gendisk;
	}

	status = register_gendisk(rd);

	if (status < 0) {
		printk(KERN_ERR "failed to register gendisk.\n");
		goto err_gendisk;
	}

	return 0;

err_gendisk:
	blk_mq_free_tag_set(&rd->tag_set);

err_init_tag_set:
	vfree(rd->block);

	return status;
}

static void free_ramdisk(struct ramdisk *rd)
{
	blk_mq_free_tag_set(&rd->tag_set);
	blk_cleanup_disk(rd->gd);
	vfree(rd->block);
}

/******************
 * ENTRY AND EXIT *
 ******************/

static int __init ramdisk_init(void)
{
	int status;

	g_major = register_blkdev(0, RAMDISK_NAME);

	if (g_major < 0) {
		printk(KERN_ERR "unable to register ramdisk block device.\n");
		return status;
	}

	status = init_ramdisk(&dev);

	if (status < 0) {
		printk(KERN_ERR "failed to initialize/create ramdisk.\n");
		return status;
	}

	return 0;
}

static void __exit ramdisk_exit(void)
{
	free_ramdisk(&dev);
	unregister_blkdev(g_major, RAMDISK_NAME);
}

module_init(ramdisk_init);
module_exit(ramdisk_exit);
