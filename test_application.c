#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <alsa/asoundlib.h>
#define _USE_MATH_DEFINES
#include <math.h>

#define BUFFER_SIZE 4096
#define AUDIO_DEVICE "/dev/audio_buffer"
#define ALSA_DEVICE "hw:Loopback,0"  // ALSA loopback device
#define SAMPLE_RATE 44100
#define CHANNELS 2
#define FORMAT SND_PCM_FORMAT_S16_LE  // 16-bit samples

// Thread function to read from our driver and write to the ALSA loopback
void *playback_thread(void *arg)
{
    int driver_fd;
    snd_pcm_t *pcm_handle;
    char buffer[BUFFER_SIZE];
    int ret;
    
    // Open our audio buffer device
    driver_fd = open(AUDIO_DEVICE, O_RDONLY);
    if (driver_fd < 0) {
        perror("Failed to open audio buffer device");
        return NULL;
    }
    
    // Open the ALSA loopback device for playback
    ret = snd_pcm_open(&pcm_handle, ALSA_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);
    if (ret < 0) {
        fprintf(stderr, "Cannot open ALSA device: %s\n", snd_strerror(ret));
        close(driver_fd);
        return NULL;
    }
    
    // Set hardware parameters
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(pcm_handle, hw_params);
    snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle, hw_params, FORMAT);
    snd_pcm_hw_params_set_channels(pcm_handle, hw_params, CHANNELS);
    unsigned int rate = SAMPLE_RATE;
snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate, 0);   
    snd_pcm_hw_params(pcm_handle, hw_params);
    
    // Calculate frames per buffer
    int frames = BUFFER_SIZE / (CHANNELS * snd_pcm_format_width(FORMAT) / 8);
    
    printf("Playback thread started. Reading from driver and playing to ALSA loopback.\n");
    
    while (1) {
        // Read from our device driver
        ssize_t bytes_read = read(driver_fd, buffer, BUFFER_SIZE);
        
        if (bytes_read <= 0) {
            // No data available, wait a bit
            usleep(100000);  // 100ms
            continue;
        }
        
        // Calculate number of frames read
        int frames_read = bytes_read / (CHANNELS * snd_pcm_format_width(FORMAT) / 8);
        
        // Write to ALSA loopback
        ret = snd_pcm_writei(pcm_handle, buffer, frames_read);
        if (ret < 0) {
            fprintf(stderr, "Write error: %s\n", snd_strerror(ret));
            snd_pcm_prepare(pcm_handle);
        } else {
            printf("Played %d frames\n", ret);
        }
    }
    
    // Cleanup
    snd_pcm_close(pcm_handle);
    close(driver_fd);
    return NULL;
}

// Thread function to generate test tones and write to our driver
void *generator_thread(void *arg)
{
    int driver_fd;
    int16_t *buffer;
    int buffer_size = BUFFER_SIZE;
    int sample_count = buffer_size / sizeof(int16_t);
    int i;
    
    // Open our audio buffer device
    driver_fd = open(AUDIO_DEVICE, O_WRONLY);
    if (driver_fd < 0) {
        perror("Failed to open audio buffer device");
        return NULL;
    }
    
    // Allocate buffer
    buffer = (int16_t *)malloc(buffer_size);
    if (!buffer) {
        perror("Failed to allocate memory");
        close(driver_fd);
        return NULL;
    }
    
    printf("Generator thread started. Generating test tone and writing to driver.\n");
    
    // Generate a simple sine wave at 440 Hz (A4 note)
    double phase = 0;
    double frequency = 440.0;
    double phase_increment = 2.0 * M_PI * frequency / SAMPLE_RATE;
    
    while (1) {
        // Generate sine wave
        for (i = 0; i < sample_count; i += CHANNELS) {
            // Sine wave for left channel
            buffer[i] = (int16_t)(32767.0 * sin(phase));
            
            // Same signal for right channel (if stereo)
            if (CHANNELS == 2) {
                buffer[i + 1] = buffer[i];
            }
            
            phase += phase_increment;
            if (phase >= 2.0 * M_PI)
                phase -= 2.0 * M_PI;
        }
        
        // Write to our device driver
        ssize_t bytes_written = write(driver_fd, buffer, buffer_size);
        if (bytes_written < 0) {
            perror("Write error");
            break;
        }
        
        printf("Generated and wrote %zd bytes of audio data\n", bytes_written);
        
        // Sleep a bit to avoid overwhelming the buffer
        usleep(500000);  // 500ms
    }
    
    // Cleanup
    free(buffer);
    close(driver_fd);
    return NULL;
}

int main()
{
    pthread_t playback_tid, generator_tid;
    int ret;
    
    // Create the generator thread
    ret = pthread_create(&generator_tid, NULL, generator_thread, NULL);
    if (ret != 0) {
        perror("Failed to create generator thread");
        return EXIT_FAILURE;
    }
    
    // Create the playback thread
    ret = pthread_create(&playback_tid, NULL, playback_thread, NULL);
    if (ret != 0) {
        perror("Failed to create playback thread");
        return EXIT_FAILURE;
    }
    
    // Wait for threads
    pthread_join(generator_tid, NULL);
    pthread_join(playback_tid, NULL);
    
    return EXIT_SUCCESS;
}
