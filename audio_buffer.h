#ifndef AUDIO_BUFFER_H
#define AUDIO_BUFFER_H

#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/wait.h>

// Audio buffer structure
struct audio_buffer_dev {
    unsigned char *buffer;         // Kernel buffer for audio data
    size_t buffer_size;            // Size of the buffer
    size_t read_pos;               // Current read position
    size_t write_pos;              // Current write position
    size_t data_size;              // Amount of data currently in buffer
    bool is_playing;               // Flag to indicate if audio is playing
    wait_queue_head_t read_queue;  // Queue for processes waiting to read
    wait_queue_head_t write_queue; // Queue for processes waiting to write
    struct mutex buffer_mutex;     // Mutex for buffer access
    struct cdev cdev;              // Character device structure
};

extern struct audio_buffer_dev *audio_device;

#endif 
