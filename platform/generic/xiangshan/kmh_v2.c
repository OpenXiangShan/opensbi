/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 Bosc
 *
 * Authors: Yangyinglu  <yangyinglu@bosc.cn>
 *          Chengbo Gao <gaochengbo@bosc.ac.cn>
 */

#include <platform_override.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_io.h>
#include <sbi/sbi_bitops.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hsm.h>
#include <sbi/sbi_pmu.h>
#include <sbi/sbi_scratch.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/irqchip/fdt_irqchip_plic.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_ipi.h>
#include <sbi/sbi_timer.h>
#include <libfdt_env.h>
#include <sbi/sbi_scratch.h>
#include <libfdt.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi/sbi_csr_detect.h>
#include "kmh_container.h"
#include "parse_dts.h"

#define CPU_N_PWRCTL_BASE(n) \
    ((volatile uint64_t *) (uintptr_t) ((n) == 0 ? 0x35080000 : \
										(n) == 1 ? 0x35380000 : \
										(n) == 2 ? 0x35180000 : \
										(n) == 3 ? 0x35480000 : 0x0))

#define PWRCTL_PWRDOWN_REQ_BIT					0
#define PWRCTL_CPU_ISO_EN_BIT					1
#define PWRCTL_CPU_SW_RST_N_BIT                 2
#define PWRCTL_PC_RESET_VECTOR_FLAG_BIT			31
#define PWRCTL_PC_RESET_VECTOR_BIT				32

#define CPU_N_PWRSTAT_BASE(n) \
    ((volatile uint64_t *) (uintptr_t) ((n) == 0 ? 0x35000000 : \
                                        (n) == 1 ? 0x35300000 : \
                                        (n) == 2 ? 0x35100000 : \
                                        (n) == 3 ? 0x35400000 : 0x0))
#define PWRSTAT_CHI_SYSCOREQ                    0
#define PWRSTAT_SYSCOACK                        1
#define PWRSTAT_CPU_HALT_BIT                    2
#define PWRSTAT_NO_OP                           3
#define PWRSTAT_PWRDOWN_ACK_BIT                 4

#define CSR_FLUSH_PWR                           0xBC1
#define FLUSH_PWR_FLUSH_L2_EN                   0
#define FLUSH_PWR_FLUSH_L2_DONE                 1

#define CSR_COREPRFETCH                         0x5C1

#define FPGA_HASH_ERROR_MASK                    0xfffffffff

#define CAUSE_ICACHE_ECC_ERROR 1
#define CAUSE_DCACHE_ECC_ERROR 2
#define CAUSE_L2CACHE_ECC_ERROR 3

struct kmh_powerdown_ipi_info {
    u32 hartid_powerdown;
};

static u32 kmh_cpu_ipi_event = SBI_IPI_EVENT_MAX;
static unsigned long ipi_powerdown_offset;
static uint64_t online_harts = 0;
static uint64_t hash = 0;
static uint64_t time = 0;

static int kmh_v2_extensions_init(const struct fdt_match *match,
                                    struct sbi_hart_features *hfeatures)
{
    return 0;
}

static bool kmh_v2_rnmi_available(void)
{
    unsigned long val;
    struct sbi_trap_info trap = {0};

    val = csr_read_allowed(CSR_MNSTATUS, (unsigned long)&trap);
    if (trap.cause)
        return false;

    val |= MNSTATUS_NMIE;
    csr_write_allowed(CSR_MNSTATUS, (unsigned long)&trap, val);
    if (trap.cause)
        return false;

    return true;
}

static void cpu_delaycycle(int cycle_count)
{
    int i;

    for (i = 0; i < cycle_count; i++)
        __asm__ __volatile("nop");
}

static int set_hart_online(u32 hartid)
{
    if (hartid < 64) {
        online_harts |= (1ULL << hartid);
    } else {
        sbi_printf("%s: Invalid error Hart %u >= 64\n", __func__, hartid);
        return SBI_ERR_FAILED;
    }

    return 0;
}

static int set_hart_offline(u32 hartid)
{
    if (hartid < 64) {
        online_harts &= ~(1ULL << hartid);
    } else {
        sbi_printf("%s: Invalid error Hart %u >= 64\n", __func__, hartid);
        return SBI_ERR_FAILED;
    }

    return 0;
}

static int is_hart_online(u32 hartid)
{
    if (hartid >= 64)
        return 0;
    return (online_harts >> hartid) & 1ULL;
}

static u32 get_online_hartid(u32 hartid_powerdown)
{
    u32 i;

    if (online_harts == 0)
        return UINT32_MAX;

    for (i = 0; i < 64; i++) {
        if (is_hart_online(i) && (i != hartid_powerdown))
            return i;
    }
    return UINT32_MAX;
}

static int kmh_cpu_hart_start(u32 hartid, ulong saddr)
{
    uint64_t value = 0;
    int rc;

    value = readq(CPU_N_PWRCTL_BASE(hartid));
    value |=  BIT(PWRCTL_PWRDOWN_REQ_BIT);
    writeq(value, CPU_N_PWRCTL_BASE(hartid));

    /* wait for power up ack */
    do {
        value = readq(CPU_N_PWRSTAT_BASE(hartid));
    } while((value & BIT(PWRSTAT_PWRDOWN_ACK_BIT)) == 0);

    /* set cpu_iso_en bit */
    value = readq(CPU_N_PWRCTL_BASE(hartid));
    value  &= ~BIT(1);
    writeq(value, CPU_N_PWRCTL_BASE(hartid));

    cpu_delaycycle(1000);
    /* set cpu_sw_rst_n bit */
    value = readq(CPU_N_PWRCTL_BASE(hartid));
    value |= BIT(PWRSTAT_CPU_HALT_BIT);
    writeq(value, CPU_N_PWRCTL_BASE(hartid));

    sbi_timer_udelay(10);
    do {
        value = readq(CPU_N_PWRSTAT_BASE(hartid));
    } while((value & BIT(PWRSTAT_CPU_HALT_BIT)) == 0);

    rc = set_hart_online(hartid);
    if (rc) {
        sbi_printf("%s: set_hart_online failed\n", __func__);
        return rc;
    }

    return 0;
}

static void core_savewarmboot_addr(void)
{
    u32 hart_id = current_hartid();
    uint64_t value = 0;
    unsigned long  entry = sbi_scratch_thishart_ptr()->warmboot_addr;

    value = readq(CPU_N_PWRCTL_BASE(hart_id));
    value |= BIT(PWRCTL_PC_RESET_VECTOR_FLAG_BIT);
    value &= 0xFFFFFFFF;
    value |= entry << PWRCTL_PC_RESET_VECTOR_BIT;
    writeq(value, CPU_N_PWRCTL_BASE(hart_id));
}

static void pre_powerdown(void)
{
    csr_write(CSR_SIE, 0x0);
    csr_write(CSR_SIP, 0x0);
    csr_write(CSR_MIE, 0x0);
    csr_write(CSR_MIP, 0x0);
    csr_write(CSR_COREPRFETCH, 0x0);
}

static void core_cache_ctrl(void)
{
    uint64_t value = 0;

    smp_mb();

    csr_set(CSR_FLUSH_PWR, BIT(0));
    value = csr_read(CSR_FLUSH_PWR);
    while((value & BIT(FLUSH_PWR_FLUSH_L2_DONE)) == 0) {
        value = csr_read(CSR_FLUSH_PWR);
    }
}

static int kmh_cpu_hart_stop(void)
{
    int rc;
    u32 hartid_control;
    u32 hartid_powerdown = current_hartid();

    core_savewarmboot_addr();
    pre_powerdown();
    smp_mb();

    hartid_control = get_online_hartid(hartid_powerdown);
    if (hartid_control == UINT32_MAX) {
        sbi_printf("%s: get_boot_hartid failed\n", __func__);
        return SBI_ERR_FAILED;
    }

    hartid_powerdown = current_hartid();
    set_hart_offline(hartid_powerdown);
    rc = sbi_ipi_send_many(1, hartid_control, kmh_cpu_ipi_event, &hartid_powerdown);
    if (rc)
        sbi_printf("%s: sbi_ipi_raw_send failed\n", __func__);
    core_cache_ctrl();
    wfi();

    sbi_printf("%s: kmh_cpu_hart_stop failed at WFI\n", __func__);

    return 0;
}

static const struct sbi_hsm_device kmh_cpu = {
    .name	      = "bosc_cpu",
    .hart_start   = kmh_cpu_hart_start,
    .hart_stop    = kmh_cpu_hart_stop,
};

static int pwrctl_ipi_update(struct sbi_scratch *scratch,
                                struct sbi_scratch *remote_scratch,
                                u32 remote_hartindex, void *data)
{
    struct kmh_powerdown_ipi_info *ipi_info;
    u32 hartid_powerdown = *(u32*)data;

    ipi_info = sbi_scratch_offset_ptr(remote_scratch, ipi_powerdown_offset);
    ipi_info->hartid_powerdown = hartid_powerdown;

    return SBI_IPI_UPDATE_SUCCESS;
}

static void pwrctl_ipi_process(struct sbi_scratch *scratch)
{
    uint64_t value = 0;
    struct kmh_powerdown_ipi_info *ipi_info;
    u32 hartid_powerdown;

    ipi_info = sbi_scratch_offset_ptr(scratch, ipi_powerdown_offset);;
    hartid_powerdown = ipi_info->hartid_powerdown;

    do {
        value = readq(CPU_N_PWRSTAT_BASE(hartid_powerdown));
    } while((value & BIT(2)) == 0);

    do {
        value = readq(CPU_N_PWRSTAT_BASE(hartid_powerdown));
    } while((value & 0x03) == 0x3);

    do {
        value = readq(CPU_N_PWRSTAT_BASE(hartid_powerdown));
    } while((value & BIT(PWRSTAT_NO_OP)) != BIT(PWRSTAT_NO_OP));

    value = readq(CPU_N_PWRCTL_BASE(hartid_powerdown));
    value &= ~BIT(PWRCTL_CPU_SW_RST_N_BIT);
    writeq(value, CPU_N_PWRCTL_BASE(hartid_powerdown));

    cpu_delaycycle(1000);
    value = readq(CPU_N_PWRCTL_BASE(hartid_powerdown));
    value |= BIT(PWRCTL_CPU_ISO_EN_BIT);
    writeq(value, CPU_N_PWRCTL_BASE(hartid_powerdown));

    cpu_delaycycle(2000);
    value = readq(CPU_N_PWRCTL_BASE(hartid_powerdown));
    value &= ~BIT(PWRCTL_PWRDOWN_REQ_BIT);
    writeq(value, CPU_N_PWRCTL_BASE(hartid_powerdown));

    do {
        value = readq(CPU_N_PWRSTAT_BASE(hartid_powerdown));
    } while((value & BIT(PWRSTAT_PWRDOWN_ACK_BIT)) != 0);
}

static struct sbi_ipi_event_ops kmh_ipi_process_ops = {
    .name	 = "IPI_PWRCTL_INJECT",
    .update  = pwrctl_ipi_update,
    .process = pwrctl_ipi_process,
};

bool dtb_has_hcontext_property()
{
    int cpus_node, len;
    const void *prop;
    const void *fdt = fdt_get_address();


    if (!fdt)
        return false;

    int root_node = fdt_path_offset(fdt, "/");
    if (root_node >= 0) {
        prop = fdt_getprop(fdt, root_node, "has_hcontext", &len);
        if (prop && len >= 0) {
            sbi_printf("DTB: Found has_hcontext in root node\n");
            return true;
        }
    }

    cpus_node = fdt_path_offset(fdt, "/cpus");
    if (cpus_node < 0) {
        sbi_printf("DTB: Failed to find cpus node\n");
        return false;
    }

    prop = fdt_getprop(fdt, cpus_node, "has_hcontext", &len);
    if (prop && len >= 0) {
        sbi_printf("DTB: Found has_hcontext property in cpus node\n");
        return true;
    }

    sbi_printf("DTB: has_hcontext property not found\n");
    return false;
}

void check_fpga_version(void)
{
    uint64_t tmp = 0;
    uint64_t value = 0;
    uint64_t mepc = 0;
    //register int ret asm ("a0") = SYSERRNO;

    value = csr_read(CSR_MSTATUS);
    csr_clear(CSR_MSTATUS, MSTATUS_MIE);

    asm volatile (
        "   .align 4\n"
        "   .option push\n"
        "   .option norvc\n"

        "   j check_fpga_version_next\n"
        "   .align 4\n"
        "check_fpga_vector:\n"
        "   csrr %[mepc], mepc\n"
        "   addi %[mepc], %[mepc], 4\n"
        "   csrw mepc, %[mepc]\n"
        "   li %[hash], 0xffffffff\n"
        "   mret\n"
        "check_fpga_version_next:\n"

        "   la %[tmp], check_fpga_vector\n"
        "   csrrw %[tmp], mtvec, %[tmp]\n"
        "   .align 4\n"
        "   li %[hash], 0x31200004\n"
        "   lw %[hash], (%[hash])\n"
        "   li %[time], 0x31200008\n"
        "   lw %[time], (%[time])\n"
        "   csrw mtvec, %[tmp]\n"

        "   .option pop\n"
        : [tmp] "+r" (tmp), [mepc] "+r" (mepc),
        [hash] "+r" (hash) , [time] "+r" (time)
        : : "memory");

    csr_write(CSR_MSTATUS, value);

    if (hash != (uint64_t)FPGA_HASH_ERROR_MASK) {
        hash &= FPGA_HASH_ERROR_MASK;
        time &= FPGA_HASH_ERROR_MASK;
    }
}

#define BEU_LOCAL_INTR    0x38010028UL
void _kmh_v2_nmi_handler(void);

static bool has_check_trigger = 0;
static void copy_config_base_to_sram(void)
{
    char *config_base = (char *)CONFIG_TEXT_ADDR;
    char *sram_base = (char *)CONFIG_SRAM_ADDR;

    sbi_memcpy(sram_base, config_base, MAX_CONFIG_SIZE);
}
static int kmh_v2_early_init(bool cold_boot,
                const struct fdt_match *match)
{


    if(dtb_has_hcontext_property())
            csr_write(CSR_HCONTEXT, 0x00);

	/*
	 * FPGA implements RNMI CSRs and can use the KMH RNMI handler directly.
	 * QEMU bosc-kmh currently does not emulate the RNMI CSR set, so forcing
	 * mtvec to the RNMI entry makes normal traps enter code that touches
	 * MNSTATUS/MNCAUSE/MNSCRATCH and hangs the VM.
	 *
	 * Only install the RNMI handler when the current hart exposes SMRNMI.
	 */
	if (kmh_v2_rnmi_available()) {
		csr_write(CSR_MTVEC, &_kmh_v2_nmi_handler);
		writeq(0,  (volatile uint64_t *)BEU_LOCAL_INTR);
		sbi_printf("%s: RNMI handler enabled\n", __func__);
	} else {
		sbi_printf("%s: SMRNMI not available, keep default mtvec for QEMU compatibility\n",
			   __func__);
	}

    check_fpga_version();

    struct platform_config cfg;
    int err;
    void *fdt = (void *)FDT_ADDR;

    if (!has_check_trigger) {
        has_check_trigger = 1;
        parse_platform_config_from_mem(&cfg);
    
        copy_config_base_to_sram();

        err = fdt_check_header(fdt);
        if (!err)
            fdt_modify(fdt, &cfg);
    }

    return 0;
}

static int kmh_v2_final_init(bool cold_boot,
                                const struct fdt_match *match)
{
    int rc = 0;
    u32 hartid = 0;
    struct kmh_powerdown_ipi_info *ipi_info;
    static bool has_print = 0;

    if (!has_print) {
        has_print = 1;
        print_string_at_addr(CONFIG_TEXT_ADDR);
    }
    //print_full_fdt();

    if(dtb_has_hcontext_property())
     {
        /* as the debug info reserved.*/
         sbi_printf("%s:  MSTATEEN0=0x%lx \n", __func__, csr_read(CSR_MSTATEEN0));
         sbi_printf("%s:  CSR_HCONTEXT=0x%lx \n", __func__, csr_read(CSR_HCONTEXT));
     }

    if (hash != (uint64_t)FPGA_HASH_ERROR_MASK)
        sbi_printf("fpga hash=0x%lx, time=0x%lx\n", hash, time);

    if (cold_boot) {
        sbi_hsm_set_device(&kmh_cpu);
    }
    
#ifdef CONFIG_SBI_CONTAINER
    if (sbi_container_init() != SBI_SUCCESS)
        sbi_hart_hang();  // 初始化失败则挂起
#endif

    if (kmh_cpu_ipi_event == SBI_IPI_EVENT_MAX) {
        ipi_powerdown_offset = sbi_scratch_alloc_offset(sizeof(*ipi_info));
        if (!ipi_powerdown_offset) {
            sbi_printf("%s: sbi_scratch_alloc_offset failed\n", __func__);
            return SBI_ENOMEM;
        }
        rc = sbi_ipi_event_create(&kmh_ipi_process_ops);
        if (rc < 0) {
            sbi_printf("%s: sbi_ipi_event_create failed\n", __func__);
            return rc;
        }
        kmh_cpu_ipi_event = rc;
    }

    hartid = current_hartid();
    rc = set_hart_online(hartid);
    if (rc) {
        sbi_printf("%s: set_hart_online failed\n", __func__);
        return rc;
    }

    return 0;
}


static const struct fdt_match kmh_v2_match[] = {
    { .compatible = "bosc,kmh-v2-dev" },
    { },
};

static int kmh_v2_pmu_init(const struct fdt_match *match)
{
    return 0;
}

const struct platform_override kmh_v2 = {
    .match_table	= kmh_v2_match,
    .early_init     = kmh_v2_early_init,
    .final_init = kmh_v2_final_init,
    .extensions_init = kmh_v2_extensions_init,
    .pmu_init = kmh_v2_pmu_init,
};
