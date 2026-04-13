#ifndef __PARSE_DTS_H__
#define __PARSE_DTS_H__

#ifndef FW_CONFIG_TEXT_ADDR
#define FW_CONFIG_TEXT_ADDR 0x90000000ULL
#endif

#define CONFIG_TEXT_ADDR    FW_CONFIG_TEXT_ADDR
#define CONFIG_SRAM_ADDR    0x37f00000ULL
#define MAX_LINE_LEN        1024
#define MAX_CONFIG_SIZE     4096
#define MAX_BOOTARGS_LEN    2048
#define UART0_NS16550       "ns16550a"
#define UART1_XLNX          "xlnx,xps-uartlite-1.00.a"
#define FDT_ADDR (FW_TEXT_START + FW_JUMP_FDT_OFFSET)  //0x80100000

struct cmd_config {
    unsigned long start_addr;
    unsigned long guest_start_addr;
    char bootargs[MAX_BOOTARGS_LEN];
};

struct mem_config {
    unsigned long start_addr;
    unsigned long size;
};

struct task_config {
    unsigned long start_addr;
    u32 offset;
};

#define MAX_UART_COMPAT_LEN 64

struct uart_config {
    char compatible[MAX_UART_COMPAT_LEN];
};

struct platform_config {
    struct mem_config mem;
    struct uart_config uart;
    struct cmd_config cmd;
    struct task_config task;
    struct task_config task_guest;
    bool mem_valid;
    bool uart_valid;
    bool cmd_valid;
    bool task_valid;
    bool task_guest_valid;
};


extern int parse_platform_config_from_mem(struct platform_config *cfg);
extern void fdt_modify(void *fdt, struct platform_config *cfg);
extern void patch_sram_task_copy(struct platform_config *cfg);
extern void patch_guest_task_copy(struct platform_config *cfg);
extern void print_string_at_addr(unsigned long addr);
extern void print_full_fdt(void);
#endif
