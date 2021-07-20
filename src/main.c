#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>

static int device_open(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, unsigned long, loff_t *);
static ssize_t device_write(struct file *, const char *, unsigned long int, loff_t *);
static int device_release(struct inode *, struct file *);

#define DEVICE_NAME "copy"
#define BUF_LEN 1000

static dev_t dev_no = 0;
static struct cdev *chardev_cdev = NULL;
static struct class *chardev_class = NULL;
static char *msg_Ptr;

static int Device_Open = 0;
static char msg[BUF_LEN];

static int mychardev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", 0777);
    return 0;
}

static struct file_operations fops = {.owner = THIS_MODULE,
                                      .open = device_open,
                                      .read = device_read,
                                      .write = device_write,
                                      .release = device_release};

static int __init start_chardev(void)
{
    int err = 0;
    /* Get a device number. Get one minor number (0) */
    if ((err = alloc_chrdev_region(&dev_no, 0, 1, DEVICE_NAME)) < 0)
    {
        printk(KERN_ERR "chardev: alloc_chrdev_region() error %d\n", err);
        return err;
    }
    chardev_class = class_create(THIS_MODULE, DEVICE_NAME);
    chardev_class->dev_uevent = mychardev_uevent;

    // Allocate and initialize the char device
    chardev_cdev = cdev_alloc();
    chardev_cdev->ops = &fops;
    chardev_cdev->owner = THIS_MODULE;

    // Add the character device to the kernel
    if ((err = cdev_add(chardev_cdev, dev_no, 1)) < 0)
    {
        printk(KERN_ERR "chardev: cdev_add() error %d\n", err);
        return err;
    }
    printk(KERN_ERR "chardev: cdev_add() error %d\n", dev_no);
    device_create(chardev_class, NULL, dev_no, NULL, DEVICE_NAME);

    return 0;
}

static void __exit stop_chardev(void)
{
    device_destroy(chardev_class, dev_no);
    cdev_del(chardev_cdev);
    class_destroy(chardev_class);
    unregister_chrdev_region(dev_no, 1);
}

/* Called when a process opens chardev */
static int device_open(struct inode *inode, struct file *file)
{
    static int counter = 0;

    if (Device_Open)
        return -EBUSY;

    Device_Open++;
    sprintf(msg, "Count %d\n", counter++);
    msg_Ptr = msg;

    try_module_get(THIS_MODULE);

    return 0;
}

/*
 * Called when a process closes the device file.
 */
static int device_release(struct inode *inode, struct file *filp)
{
    Device_Open--;
    module_put(THIS_MODULE);

    return 0;
}

/* Called when a process reads from chardev. Returns, and sets *offset to, the number of bytes read. */
static ssize_t device_read(struct file *filp, char *buffer, unsigned long length, loff_t *offset)
{
    /*
   * Number of bytes actually written to the buffer
   */
    int bytes_read = 0;

    /*
   * If we're at the end of the message, return 0 signifying end of file.
   */
    if (*msg_Ptr == 0)
        return 0;

    /*
   * Actually put the data into the buffer
   */
    while (length && *msg_Ptr)
    {
        /*
     * The buffer is in the user data segment, not the kernel segment so "*"
     * assignment won't work. We have to use put_user which copies data from the
     * kernel data segment to the user data segment.
     */
        put_user(*(msg_Ptr++), buffer++);
        length--;
        bytes_read++;
    }

    /*
   * Most read functions return the number of bytes put into the buffer
   */
    return bytes_read;
}

static struct file *file_open(const char *path, int flags, int rights)
{
    struct file *filp = NULL;
    mm_segment_t oldfs;
    int err = 0;

    oldfs = get_fs();
    set_fs(get_ds());
    filp = filp_open(path, flags, rights);
    set_fs(oldfs);
    if (IS_ERR(filp))
    {
        err = PTR_ERR(filp);
        printk("Couldn't create file for writeing %d\n", err);
        return NULL;
    }
    return filp;
}

void file_close(struct file *file)
{
    filp_close(file, NULL);
}

static int file_write(struct file *file, unsigned long long offset, unsigned char *data, unsigned long size)
{
    mm_segment_t oldfs;
    int ret;
    oldfs = get_fs();
    set_fs(get_ds());
    ret = vfs_write(file, data, size, &offset);
    set_fs(oldfs);
    return ret;
}

/*
 * Called when a process writes to dev file: echo "hi" > /dev/hello
 */
static ssize_t
device_write(struct file *filp, const char *buf, size_t count, loff_t *off)
{
    size_t maxdatalen = 1000, ncopied;

    if (count < maxdatalen)
    {
        maxdatalen = count;
    }
    unsigned char databuf[maxdatalen];

    ncopied = copy_from_user(databuf, buf, maxdatalen);

    if (ncopied == 0)
    {
        printk("Copied %zd bytes from the user\n", maxdatalen);
    }
    else
    {
        printk("Could't copy %zd bytes from the user\n", ncopied);
    }

    databuf[maxdatalen] = 0;

    //printk("Data from the user: %s\n", databuf);
    size_t i = 0;
    for (i = 0; i < count / 8 + 1; i++)
    {
        unsigned char tmpbuffer[24]; //3*8
        int k;
        int j = 0;
        for (k = 0; k < 7; k++)
        {
            if (i * 8 + k < count)
            {
                j += sprintf(tmpbuffer + j, "%02x ", databuf[i * 8 + k]);
            }
        }
        if (i * 8 + 7 < count)
        {
            j += sprintf(tmpbuffer + j, "%02x\n", databuf[i * 8 + 7]);
        }
        struct file *f = file_open("/tmp/output", O_CREAT | O_RDWR | O_APPEND, 0);
        if (!f)
        {
            printk("Couldn't create File for writeing data\n");
            return -1;
        }
        file_write(f, 24 * i, tmpbuffer, j);
        file_close(f);
    }

    return count;
}

MODULE_LICENSE("GPL");
module_init(start_chardev);
module_exit(stop_chardev);