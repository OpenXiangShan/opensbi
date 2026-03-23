#include <sbi/riscv_io.h>
#include <sbi/sbi_error.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/serial/fdt_serial.h>
#include <sbi_utils/serial/virtio_console.h>
#include <sbi/sbi_console.h>

static int serial_virtio_console_init(void *fdt, int nodeoff,
				      const struct fdt_match *match)
{
	int rc;
	int device_id;
	struct platform_uart_data uart = { 0 };

	rc = fdt_parse_uart_node(fdt, nodeoff, &uart);
	if (rc)
		return rc;

	device_id = readl((void *)uart.addr + VIRTIO_MMIO_DEVICE_ID);
	if (device_id != VIRTIO_ID_CONSOLE)
		return SBI_ENODEV;

	return virtio_console_init(uart.addr);
}

static const struct fdt_match serial_virtio_console_match[] = {
	{ .compatible = "virtio,mmio" },
	{ },
};

struct fdt_serial fdt_serial_virtio_console = {
	.match_table = serial_virtio_console_match,
	.init = serial_virtio_console_init,
};
