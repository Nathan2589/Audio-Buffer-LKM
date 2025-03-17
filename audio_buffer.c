#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/export.h>
#include "proc_audio.h"
#include "audio_buffer.h"

#define DEVICE_NAME "audio_buffer"
#define CLASS_NAME  "audio"
#define BUFFER_SIZE (512 * 1024)  // 512 KB buffer
#define SAMPLE_RATE 44100
#define CHANNELS    2
#define FRAME_BYTES 4  // 16-bit stereo = 4 bytes per frame

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Audio Buffer Kernel Module");


// Define ioctl commands
#define AUDIO_BUFFER_IOCTL_MAGIC 'a'
#define AUDIO_BUFFER_IOCTL_RESET _IO(AUDIO_BUFFER_IOCTL_MAGIC, 0)
#define AUDIO_BUFFER_IOCTL_GET_SIZE _IOR(AUDIO_BUFFER_IOCTL_MAGIC, 1, size_t)
#define AUDIO_BUFFER_IOCTL_SET_SIZE _IOW(AUDIO_BUFFER_IOCTL_MAGIC, 2, size_t)


static int major_number;
static struct class *audio_class = NULL;
struct audio_buffer_dev *audio_device = NULL;
EXPORT_SYMBOL(audio_device);
// function prototypes
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);
void proc_init(void);
void proc_cleanup(void);
static long device_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);

static struct file_operations fops = {
    .open = device_open,
    .release = device_release,
    .read = device_read,
    .write = device_write,
    .unlocked_ioctl = device_ioctl,
};

static int __init audio_buffer_init(void)
{
    dev_t dev = 0;
    int result;
    
    printk(KERN_INFO "Audio Buffer: Initializing the module\n");
    
    // allocate device numbers
    result = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    if (result < 0) {
        printk(KERN_ALERT "Audio Buffer: Failed to allocate device numbers\n");
        return result;
    }
    major_number = MAJOR(dev);
    
    // create device class
    audio_class = class_create(CLASS_NAME);
    if (IS_ERR(audio_class)) {
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "Audio Buffer: Failed to create device class\n");
        return PTR_ERR(audio_class);
    }
    
    // allocate the device structure
    audio_device = kmalloc(sizeof(struct audio_buffer_dev), GFP_KERNEL);
    if (!audio_device) {
        class_destroy(audio_class);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "Audio Buffer: Failed to allocate device structure\n");
        return -ENOMEM;
    }
    
    // initialize the device structure
    audio_device->buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!audio_device->buffer) {
        kfree(audio_device);
        class_destroy(audio_class);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "Audio Buffer: Failed to allocate buffer memory\n");
        return -ENOMEM;
    }
    
    audio_device->buffer_size = BUFFER_SIZE;
    audio_device->read_pos = 0;
    audio_device->write_pos = 0;
    audio_device->data_size = 0;
    audio_device->is_playing = false;
    
    init_waitqueue_head(&audio_device->read_queue);
    init_waitqueue_head(&audio_device->write_queue);
    mutex_init(&audio_device->buffer_mutex);
    
    // initialize the character device
    cdev_init(&audio_device->cdev, &fops);
    audio_device->cdev.owner = THIS_MODULE;
    
    // Add the device to the system
    result = cdev_add(&audio_device->cdev, dev, 1);
    if (result < 0) {
        kfree(audio_device->buffer);
        kfree(audio_device);
        class_destroy(audio_class);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "Audio Buffer: Failed to add device to system\n");
        return result;
    }
    
    // Create the device node in /dev
    if (IS_ERR(device_create(audio_class, NULL, dev, NULL, DEVICE_NAME))) {
        cdev_del(&audio_device->cdev);
        kfree(audio_device->buffer);
        kfree(audio_device);
        class_destroy(audio_class);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "Audio Buffer: Failed to create device node\n");
        return PTR_ERR(audio_class);
    }

    proc_init();  // Initialize the proc file
    
    printk(KERN_INFO "Audio Buffer: Device initialized successfully with major number %d\n", major_number);
    return 0;
}

static void __exit audio_buffer_exit(void)
{

    proc_cleanup();
    
    device_destroy(audio_class, MKDEV(major_number, 0));
    cdev_del(&audio_device->cdev);
    
    kfree(audio_device->buffer);
    kfree(audio_device);
    
    class_destroy(audio_class);
    unregister_chrdev_region(MKDEV(major_number, 0), 1);
    
    printk(KERN_INFO "Audio Buffer: Module unloaded\n");
}

static int device_open(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO "Audio Buffer: Device opened\n");
    return 0;
}

static int device_release(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO "Audio Buffer: Device closed\n");
    return 0;
}

static ssize_t device_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    size_t bytes_to_copy;
    int ret;
    
    // Lock the buffer
    if (mutex_lock_interruptible(&audio_device->buffer_mutex))
        return -ERESTARTSYS;
    
    // Wait for data if empty
    while (audio_device->data_size == 0) {
        mutex_unlock(&audio_device->buffer_mutex);
        
        if (filep->f_flags & O_NONBLOCK)
            return -EAGAIN;
        
        if (wait_event_interruptible(audio_device->read_queue, audio_device->data_size > 0))
            return -ERESTARTSYS;
        
        if (mutex_lock_interruptible(&audio_device->buffer_mutex))
            return -ERESTARTSYS;
    }
    
    // Calculate how many bytes to copy
    bytes_to_copy = min(len, audio_device->data_size);
    
    // Handle buffer wraparound
    if (audio_device->read_pos + bytes_to_copy > audio_device->buffer_size) {
        size_t first_chunk = audio_device->buffer_size - audio_device->read_pos;
        
        // Copy the first chunk (up to the end of the buffer)
        ret = copy_to_user(buffer, audio_device->buffer + audio_device->read_pos, first_chunk);
        if (ret) {
            mutex_unlock(&audio_device->buffer_mutex);
            return -EFAULT;
        }
        
        // Copy the second chunk (from the beginning of the buffer)
        ret = copy_to_user(buffer + first_chunk, audio_device->buffer, bytes_to_copy - first_chunk);
        if (ret) {
            mutex_unlock(&audio_device->buffer_mutex);
            return -EFAULT;
        }
        
        audio_device->read_pos = bytes_to_copy - first_chunk;
    } else {
        // Simple case - no wraparound
        ret = copy_to_user(buffer, audio_device->buffer + audio_device->read_pos, bytes_to_copy);
        if (ret) {
            mutex_unlock(&audio_device->buffer_mutex);
            return -EFAULT;
        }
        
        audio_device->read_pos += bytes_to_copy;
        if (audio_device->read_pos == audio_device->buffer_size)
            audio_device->read_pos = 0;
    }
    
    // Update the data size
    audio_device->data_size -= bytes_to_copy;
    
    // Wake up any writers waiting for space
    mutex_unlock(&audio_device->buffer_mutex);
    wake_up_interruptible(&audio_device->write_queue);
    
    printk(KERN_INFO "Audio Buffer: Read %zu bytes\n", bytes_to_copy);
    return bytes_to_copy;
}

static ssize_t device_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    size_t bytes_to_copy;
    size_t space_available;
    int ret;
    
    // Lock the buffer
    if (mutex_lock_interruptible(&audio_device->buffer_mutex))
        return -ERESTARTSYS;
    
    // Calculate available space
    space_available = audio_device->buffer_size - audio_device->data_size;
    
    // Wait if the buffer is full
    while (space_available == 0) {
        mutex_unlock(&audio_device->buffer_mutex);
        
        if (filep->f_flags & O_NONBLOCK)
            return -EAGAIN;
        
        if (wait_event_interruptible(audio_device->write_queue, 
                                    (audio_device->buffer_size - audio_device->data_size) > 0))
            return -ERESTARTSYS;
        
        if (mutex_lock_interruptible(&audio_device->buffer_mutex))
            return -ERESTARTSYS;
        
        space_available = audio_device->buffer_size - audio_device->data_size;
    }
    
    // Calculate how many bytes to copy
    bytes_to_copy = min(len, space_available);
    
    // Handle buffer wraparound
    if (audio_device->write_pos + bytes_to_copy > audio_device->buffer_size) {
        size_t first_chunk = audio_device->buffer_size - audio_device->write_pos;
        
        // Copy the first chunk (up to the end of the buffer)
        ret = copy_from_user(audio_device->buffer + audio_device->write_pos, buffer, first_chunk);
        if (ret) {
            mutex_unlock(&audio_device->buffer_mutex);
            return -EFAULT;
        }
        
        // Copy the second chunk (from the beginning of the buffer)
        ret = copy_from_user(audio_device->buffer, buffer + first_chunk, bytes_to_copy - first_chunk);
        if (ret) {
            mutex_unlock(&audio_device->buffer_mutex);
            return -EFAULT;
        }
        
        audio_device->write_pos = bytes_to_copy - first_chunk;
    } else {
        // Simple case - no wraparound
        ret = copy_from_user(audio_device->buffer + audio_device->write_pos, buffer, bytes_to_copy);
        if (ret) {
            mutex_unlock(&audio_device->buffer_mutex);
            return -EFAULT;
        }
        
        audio_device->write_pos += bytes_to_copy;
        if (audio_device->write_pos == audio_device->buffer_size)
            audio_device->write_pos = 0;
    }
    
    // Update the data size and set playing flag
    audio_device->data_size += bytes_to_copy;
    audio_device->is_playing = true;
    
    // Wake up any readers waiting for data
    mutex_unlock(&audio_device->buffer_mutex);
    wake_up_interruptible(&audio_device->read_queue);
    
    printk(KERN_INFO "Audio Buffer: Wrote %zu bytes\n", bytes_to_copy);
    return bytes_to_copy;
}

static long device_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
    size_t new_size;
    void *new_buffer;
    int ret = 0;

    switch(cmd){
        case AUDIO_BUFFER_IOCTL_RESET:
            //Resets the audio buffer
            mutex_lock(&audio_device->buffer_mutex);
            audio_device->read_pos=0;
            audio_device->write_pos=0;
            audio_device->data_size=0;
            audio_device->is_playing=false;
            mutex_unlock(&audio_device->buffer_mutex);
            printk(KERN_INFO "Audio Buffer: Buffer reset\n");
            break;
        case AUDIO_BUFFER_IOCTL_GET_SIZE:
            ret = copy_to_user((size_t __user *)arg, &audio_device->buffer_size, sizeof(size_t));
            if(ret){
                printk(KERN_ERR "Audio Buffer: failed to get buffer size");
                return -EFAULT;
            }
            break;
        case AUDIO_BUFFER_IOCTL_SET_SIZE:
            //coppies the new buffer size to the user
            ret = copy_to_user(&new_size, (size_t __user *)arg, sizeof(size_t));
            if(ret){
                printk(KERN_ERR "Audio Buffer: failed to set buffer size");
                return -EFAULT;
            }
            //checks if its above the max buffer size
            if(new_size == 0 || new_size > BUFFER_SIZE){
                printk(KERN_ERR "Audio Buffer: new size cannot be 0 or greater than the buffer size.");
                return -EINVAL;
            }

            new_buffer = kmalloc(new_size, GFP_KERNEL);
            if(!new_buffer)
            {
                printk(KERN_ERR "Audio Buffer: Failed to allocate new buffer");
                return -ENOMEM;
            }

            //Sets the buffer size and resets the audio_device
            mutex_lock(&audio_device->buffer_mutex);
            kfree(audio_device->buffer);
            audio_device->buffer = new_buffer;
            audio_device->buffer_size = new_size;
            audio_device->read_pos = 0;
            audio_device->write_pos = 0;
            audio_device->data_size = 0;
            audio_device->is_playing = false;
            mutex_unlock(&audio_device->buffer_mutex);
            printk(KERN_INFO "Audio Buffer: new size set to %zu\n", new_size);
        
            break;
        default:
            return -ENOTTY;
        
    }
    return 0;
}


module_init(audio_buffer_init);
module_exit(audio_buffer_exit);
