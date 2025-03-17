#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/timekeeping.h>
#include <linux/mutex.h>
#include "proc_audio.h"
#include "audio_buffer.h"

extern struct audio_buffer_dev *audio_device;  // Use the existing audio buffer

static struct proc_dir_entry *proc_entry;  // /proc file entry

static unsigned int buffer_overruns = 0;  
static unsigned int buffer_underruns = 0;  

static struct timespec64 last_write_time;
static struct timespec64 last_read_time;

// Function to display content in /proc file
static int my_proc_show(struct seq_file *m, void *v) {
    mutex_lock(&audio_device->buffer_mutex);  // Lock the buffer while reading stats

    seq_printf(m, "Audio Buffer Module Stats:\n");
    seq_printf(m, "Last Read Time: %lld.%09ld\n", last_read_time.tv_sec, last_read_time.tv_nsec);
    seq_printf(m, "Last Write Time: %lld.%09ld\n", last_write_time.tv_sec, last_write_time.tv_nsec);
    seq_printf(m, "Total Buffer Size: %zu bytes\n", audio_device->buffer_size);
    seq_printf(m, "Current Buffer Usage: %zu bytes\n", audio_device->data_size);
    seq_printf(m, "Available Buffer Space: %zu bytes\n", audio_device->buffer_size - audio_device->data_size);
    seq_printf(m, "Buffer Overruns: %u\n", buffer_overruns);
    seq_printf(m, "Buffer Underruns: %u\n", buffer_underruns);
    
    mutex_unlock(&audio_device->buffer_mutex);  // Unlock after reading
    return 0;
}

// Open handler for /proc
static int my_proc_open(struct inode *inode, struct file *file) {
    return single_open(file, my_proc_show, NULL);
}

// Function to write data to the audio buffer
void write_to_buffer(char data) {
    mutex_lock(&audio_device->buffer_mutex);  // Lock before modifying buffer

    if (audio_device->data_size < audio_device->buffer_size) {
        audio_device->buffer[audio_device->write_pos] = data;
        audio_device->write_pos = (audio_device->write_pos + 1) % audio_device->buffer_size;
        audio_device->data_size++;
        ktime_get_real_ts64(&last_write_time);
    } else {
        buffer_overruns++;
    }

    mutex_unlock(&audio_device->buffer_mutex);  // Unlock after modification
}

// Function to read data from the audio buffer
int read_from_buffer(void) {
    char data;
    mutex_lock(&audio_device->buffer_mutex);  // Lock before modifying buffer

    if (audio_device->data_size > 0) {
        data = audio_device->buffer[audio_device->read_pos];
        audio_device->read_pos = (audio_device->read_pos + 1) % audio_device->buffer_size;
        audio_device->data_size--;
        ktime_get_real_ts64(&last_read_time);
    } else {
        buffer_underruns++;
        data = -1;  // Indicate an empty buffer
    }

    mutex_unlock(&audio_device->buffer_mutex);  // Unlock after modification
    return data;
}

// File operations for the proc file
static const struct proc_ops my_proc_fops = {
    .proc_open    = my_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

// Initialize proc file
void proc_init(void) {
    proc_entry = proc_create("my_stats", 0666, NULL, &my_proc_fops);
    if (!proc_entry) {
        printk(KERN_ERR "my_proc: Failed to create /proc/my_stats\n");
    } else {
        printk(KERN_INFO "my_proc: Created /proc/my_stats\n");
    }
}

// Cleanup proc file
void proc_cleanup(void) {
    proc_remove(proc_entry);
    printk(KERN_INFO "my_proc: Removed /proc/my_stats\n");
}
