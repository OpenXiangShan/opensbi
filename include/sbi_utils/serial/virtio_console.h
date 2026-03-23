#ifndef __VIRTIO_CONSOLE_H__
#define __VIRTIO_CONSOLE_H__

#define VIRTIO_MMIO_DEVICE_ID		0x008
#define VIRTIO_MMIO_HOST_FEATURES	0x010
#define VIRTIO_MMIO_CONFIG		0x100

#define VIRTIO_ID_CONSOLE		3
#define VIRTIO_CONSOLE_F_EMERG_WRITE 	2

struct virtio_console_config {
	/* colums of the screens */
	u16 cols;
	/* rows of the screens */
	u16 rows;
	/* max. number of ports this device can hold */
	u32 max_nr_ports;
	/* emergency write register */
	u32 emerg_wr;
} __attribute__((packed));

int virtio_console_init(unsigned long base);

#endif
