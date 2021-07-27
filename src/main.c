#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>


static int device_open(struct inode *, struct file *);
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

static int mychardev_uevent(struct device * dev, struct kobj_uevent_env *env)
{
    add_uevent_var(env, "DEVMODE=%#o", 0777);
    return 0;
}

static struct file_operations fops = {.owner = THIS_MODULE,
                                      .open = device_open,
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
    static int counter;
    counter = 0;

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
 * Called when a process writes to dev file: echo "hi" > /dev/copy
 */
static ssize_t
device_write(struct file *filp, const char *buf, size_t count, loff_t *pos)
{
    size_t ncopied;
    size_t i;
    struct file *filePtr;
    int numberOfWrittenBytes;
    unsigned char *databuf = (unsigned char*)vzalloc(count);
    if(!databuf)
    {
        printk("Couldn't allocate heap memoray\n");
        return -1;
    }

    ncopied = copy_from_user(databuf, buf, count);

    if (ncopied)
    {
        printk("Copied %zd bytes from the user\n", count);
    }
    filePtr = file_open("/tmp/output", O_CREAT | O_RDWR, 0);
    if (!filePtr)
    {
        printk("Couldn't create File for writeing data\n");
        vfree(databuf);
        return -1;
    }
    for (i = 0; i < count / 8 + 1; i++)
    {
        //Would be faster to write in a file using 24 bytes of chunks instead of writeing byte by byte
        //sprintf returns 3 after writeing, so in case of 8 iteration the needed buffersize would be 8 * 3
        unsigned char tmpbuffer[24]; 
        int k;
        int j = 0;
        for (k = 0; k < 7; k++)
        {
            //needed to be checked every time for avoiding out of range databuf[.]
            if (i * 8 + k < count)
            {
                j += sprintf(tmpbuffer + j, "%02x ", databuf[i * 8 + k]);
            }
        }
        //8 bytes written and needed to put "\n"
        if (i * 8 + 7 < count)
        {
            j += sprintf(tmpbuffer + j, "%02x\n", databuf[i * 8 + 7]);
        }
       

        numberOfWrittenBytes = file_write(filePtr, i * 24, tmpbuffer, j);
        if(numberOfWrittenBytes == 0)
        {
            printk("Couldn't write data to file\n");
        }
        else {
            printk("Written %d bytes to the file\n", numberOfWrittenBytes);
        }
    }
    file_close(filePtr);
    vfree(databuf);
    return count;
}

MODULE_LICENSE("GPL");
module_init(start_chardev);
module_exit(stop_chardev);