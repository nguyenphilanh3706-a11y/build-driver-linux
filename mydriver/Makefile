obj-m += smartled_driver.o
obj-m += sound_driver.o
obj-m += fake_device.o
obj-m += i2s_codec_driver.o
obj-m += hdmi_audio_driver.o
obj-m += mic_audio_driver.o
obj-m += speaker_audio_driver.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean