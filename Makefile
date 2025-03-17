obj-m += audio_module.o

audio_module-objs := audio_buffer.o proc_audio.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
