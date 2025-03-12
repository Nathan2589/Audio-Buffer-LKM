# Audio-Buffer-LKM
To set up:
- make
- gcc -o test_application test_application.c -lasound -lm -pthread
- sudo insmod audio_buffer.ko
- check if the module loaded correctly using dmesg | tail
- Verifying functionality using virtual hardware:
  - ./test_application
  - arecord -D hw:Loopback,1 -f S16_LE -r 44100 -c 2 -d 10 test.wav
  - aplay test.wav
