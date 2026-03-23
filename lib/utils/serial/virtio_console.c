#include <sbi/riscv_asm.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_domain.h>
#include <sbi_utils/serial/virtio_console.h>

static unsigned long base_addr;

#if 0
static bool virtio_console_can_getc(unsigned long base)
{
	struct virtio_console_config *p = (void *)base + VIRTIO_MMIO_CONFIG;

	return (readl(&p->emerg_wr) & (1 << 31)) ? true : false;
}
#endif

static void virtio_console_putc(char ch)
{
	u32 tmp;
	unsigned long base = base_addr;
	struct virtio_console_config *p = (void *)base + VIRTIO_MMIO_CONFIG;

	tmp = readl((void *)(base + VIRTIO_MMIO_HOST_FEATURES));
	if (!(tmp & (1 << VIRTIO_CONSOLE_F_EMERG_WRITE))) {
		return;
	}

	writel(ch, &p->emerg_wr);
}

static int virtio_console_getc(void)
{
	return -1;
}

static struct sbi_console_device virtio_console = {
	.name = "virtio_console",
	.console_putc = virtio_console_putc,
	.console_getc = virtio_console_getc,
};

int virtio_console_init(unsigned long base)
{
	base_addr = base;

//	if (!virtio_console_can_getc(base))
//		return -1;

	sbi_console_set_device(&virtio_console);

	return sbi_domain_root_add_memrange(base, PAGE_SIZE, PAGE_SIZE,
					    (SBI_DOMAIN_MEMREGION_MMIO |
					    SBI_DOMAIN_MEMREGION_SHARED_SURW_MRW));
}
