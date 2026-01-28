/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *   Anup Patel <anup.patel@wdc.com>
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_atomic.h>
#include <sbi/riscv_barrier.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_domain.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_hartmask.h>
#include <sbi/sbi_hsm.h>
#include <sbi/sbi_ipi.h>
#include <sbi/sbi_irqchip.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_pmu.h>
#include <sbi/sbi_system.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_timer.h>
#include <sbi/sbi_tlb.h>
#include <sbi/sbi_version.h>

#include <sbi_elf.c>

// Neelu: Disable the following to skip loading and launching Tyche and directly launch Linux.  
#define LAUNCH_TYCHE 

#ifdef LAUNCH_TYCHE 
#define TYCHE_LOAD_ADDRESS 0x0000000080450000    
#define TYCHE_MANIFEST_ADDRESS 0x0000000080440000
#define ADDRESS_MASK_BITS 32

extern int tyche_sm_bin;

struct tyche_manifest {
    unsigned long next_arg1;
    unsigned long next_addr;
    unsigned long next_mode;
    unsigned long coldboot_hartid;
    unsigned long num_harts;
};

// TYCHE_LOAD_ADDRESS, manifest->next_addr - TYCHE_LOAD_ADDRESS - 1, tyche_start, hartid, manifest
void enter_anchor(unsigned long tyche_start_addr, unsigned long tyche_region_size, unsigned long tyche_entry_addr, unsigned long hartid, struct tyche_manifest* manifest) {

	//sbi_printf("\nArgs to Anchor: tyche_start_addr: %lx, region_size: %lx, entry_addr: %lx, hartid: %lx, manifest_addr: %p", tyche_start_addr, tyche_region_size, tyche_entry_addr, hartid, manifest); 

	// register unsigned long r_sa0 asm("a0") = TYCHE_LOAD_ADDRESS;
	// register unsigned long r_sa1 asm("a1") = tyche_region_size;
	// register unsigned long r_sa2 asm("a2") = tyche_entry_addr;
	// register unsigned long r_sa3 asm("a3") = hartid;
	// register struct tyche_manifest *r_sa4 asm("a4") = manifest;
	// //register unsigned long r_sa0 asm("a5") = TYCHE_LOAD_ADDRESS;
	// //register unsigned long r_sa2 asm("a6") = tyche_entry_addr;

	// __asm__ __volatile__ (
	// 	// directly use them, no mv needed
	// 	".word 0x0002902b\n\t"
	// 	:
	// 	: "r"(r_sa0), "r"(r_sa1), "r"(r_sa2), "r"(r_sa3), "r"(r_sa4)
	// 	//: "a0","a1","a2","a3","a4","a5","a6","a7"
	// );

	//tyche_start_addr = ((tyche_start_addr << ADDRESS_MASK_BITS) >> ADDRESS_MASK_BITS);

	__asm__ __volatile__ (
		"mv a0, %[sa0]\n\t" 
		"mv a1, %[sa1]\n\t"
		"mv a2, %[sa2]\n\t"
		"mv a3, %[sa3]\n\t"
		"mv a4, %[sa4]\n\t"
		"mv a5, %[sa0]\n\t"
		"mv a6, %[sa2]\n\t"
		"addi a7, zero, 1\n\t"
		".word 0x0002902b\n\t"
		:
		: [sa0]"r"(TYCHE_LOAD_ADDRESS), [sa1]"r"(tyche_region_size), [sa2]"r"(tyche_entry_addr), [sa3]"r"(hartid), [sa4]"r"(manifest)
		: "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
	);

}

#endif 

//typedef void tyche_start(unsigned long, unsigned long, unsigned long, unsigned long); 

#define BANNER                                              \
	"   ____                    _____ ____ _____\n"     \
	"  / __ \\                  / ____|  _ \\_   _|\n"  \
	" | |  | |_ __   ___ _ __ | (___ | |_) || |\n"      \
	" | |  | | '_ \\ / _ \\ '_ \\ \\___ \\|  _ < | |\n" \
	" | |__| | |_) |  __/ | | |____) | |_) || |_\n"     \
	"  \\____/| .__/ \\___|_| |_|_____/|____/_____|\n"  \
	"        | |\n"                                     \
	"        |_|\n\n"

static void sbi_boot_print_banner(struct sbi_scratch *scratch)
{
	if (scratch->options & SBI_SCRATCH_NO_BOOT_PRINTS)
		return;

#ifdef OPENSBI_VERSION_GIT
	sbi_printf("\nOpenSBI %s\n", OPENSBI_VERSION_GIT);
#else
	sbi_printf("\nOpenSBI v%d.%d\n", OPENSBI_VERSION_MAJOR,
		   OPENSBI_VERSION_MINOR);
#endif

#ifdef OPENSBI_BUILD_TIME_STAMP
	sbi_printf("Build time: %s\n", OPENSBI_BUILD_TIME_STAMP);
#endif

#ifdef OPENSBI_BUILD_COMPILER_VERSION
	sbi_printf("Build compiler: %s\n", OPENSBI_BUILD_COMPILER_VERSION);
#endif

	sbi_printf(BANNER);
}

static void sbi_boot_print_general(struct sbi_scratch *scratch)
{
	char str[128];
	const struct sbi_pmu_device *pdev;
	const struct sbi_hsm_device *hdev;
	const struct sbi_ipi_device *idev;
	const struct sbi_timer_device *tdev;
	const struct sbi_console_device *cdev;
	const struct sbi_system_reset_device *srdev;
	const struct sbi_platform *plat = sbi_platform_ptr(scratch);

	if (scratch->options & SBI_SCRATCH_NO_BOOT_PRINTS)
		return;

	/* Platform details */
	sbi_printf("Platform Name             : %s\n",
		   sbi_platform_name(plat));
	sbi_platform_get_features_str(plat, str, sizeof(str));
	sbi_printf("Platform Features         : %s\n", str);
	sbi_printf("Platform HART Count       : %u\n",
		   sbi_platform_hart_count(plat));
	idev = sbi_ipi_get_device();
	sbi_printf("Platform IPI Device       : %s\n",
		   (idev) ? idev->name : "---");
	tdev = sbi_timer_get_device();
	sbi_printf("Platform Timer Device     : %s @ %luHz\n",
		   (tdev) ? tdev->name : "---",
		   (tdev) ? tdev->timer_freq : 0);
	cdev = sbi_console_get_device();
	sbi_printf("Platform Console Device   : %s\n",
		   (cdev) ? cdev->name : "---");
	hdev = sbi_hsm_get_device();
	sbi_printf("Platform HSM Device       : %s\n",
		   (hdev) ? hdev->name : "---");
	pdev = sbi_pmu_get_device();
	sbi_printf("Platform PMU Device       : %s\n",
		   (pdev) ? pdev->name : "---");
	srdev = sbi_system_reset_get_device(SBI_SRST_RESET_TYPE_COLD_REBOOT, 0);
	sbi_printf("Platform Reboot Device    : %s\n",
		   (srdev) ? srdev->name : "---");
	srdev = sbi_system_reset_get_device(SBI_SRST_RESET_TYPE_SHUTDOWN, 0);
	sbi_printf("Platform Shutdown Device  : %s\n",
		   (srdev) ? srdev->name : "---");

	/* Firmware details */
	sbi_printf("Firmware Base             : 0x%lx\n", scratch->fw_start);
	sbi_printf("Firmware Size             : %d KB\n",
		   (u32)(scratch->fw_size / 1024));

	/* SBI details */
	sbi_printf("Runtime SBI Version       : %d.%d\n",
		   sbi_ecall_version_major(), sbi_ecall_version_minor());
	sbi_printf("\n");
}

static void sbi_boot_print_domains(struct sbi_scratch *scratch)
{
	if (scratch->options & SBI_SCRATCH_NO_BOOT_PRINTS)
		return;

	/* Domain details */
	sbi_domain_dump_all("      ");
}

static void sbi_boot_print_hart(struct sbi_scratch *scratch, u32 hartid)
{
	int xlen;
	char str[128];
	const struct sbi_domain *dom = sbi_domain_thishart_ptr();

	if (scratch->options & SBI_SCRATCH_NO_BOOT_PRINTS)
		return;

	/* Determine MISA XLEN and MISA string */
	xlen = misa_xlen();
	if (xlen < 1) {
		sbi_printf("Error %d getting MISA XLEN\n", xlen);
		sbi_hart_hang();
	}

	/* Boot HART details */
	sbi_printf("Boot HART ID              : %u\n", hartid);
	sbi_printf("Boot HART Domain          : %s\n", dom->name);
	sbi_hart_get_priv_version_str(scratch, str, sizeof(str));
	sbi_printf("Boot HART Priv Version    : %s\n", str);
	misa_string(xlen, str, sizeof(str));
	sbi_printf("Boot HART Base ISA        : %s\n", str);
	sbi_hart_get_extensions_str(scratch, str, sizeof(str));
	sbi_printf("Boot HART ISA Extensions  : %s\n", str);
	sbi_printf("Boot HART PMP Count       : %d\n",
		   sbi_hart_pmp_count(scratch));
	sbi_printf("Boot HART PMP Granularity : %lu\n",
		   sbi_hart_pmp_granularity(scratch));
	sbi_printf("Boot HART PMP Address Bits: %d\n",
		   sbi_hart_pmp_addrbits(scratch));
	sbi_printf("Boot HART MHPM Count      : %d\n",
		   sbi_hart_mhpm_count(scratch));
	sbi_hart_delegation_dump(scratch, "Boot HART ", "         ");
}

static spinlock_t coldboot_lock = SPIN_LOCK_INITIALIZER;
static struct sbi_hartmask coldboot_wait_hmask = { 0 };

static unsigned long coldboot_done;

static void wait_for_coldboot(struct sbi_scratch *scratch, u32 hartid)
{
	unsigned long saved_mie, cmip;

	/* Save MIE CSR */
	saved_mie = csr_read(CSR_MIE);

	/* Set MSIE and MEIE bits to receive IPI */
	csr_set(CSR_MIE, MIP_MSIP | MIP_MEIP);

	/* Acquire coldboot lock */
	spin_lock(&coldboot_lock);

	/* Mark current HART as waiting */
	sbi_hartmask_set_hart(hartid, &coldboot_wait_hmask);

	/* Release coldboot lock */
	spin_unlock(&coldboot_lock);

	/* Wait for coldboot to finish using WFI */
	while (!__smp_load_acquire(&coldboot_done)) {
		do {
			wfi();
			cmip = csr_read(CSR_MIP);
		 } while (!(cmip & (MIP_MSIP | MIP_MEIP)));
	};

	/* Acquire coldboot lock */
	spin_lock(&coldboot_lock);

	/* Unmark current HART as waiting */
	sbi_hartmask_clear_hart(hartid, &coldboot_wait_hmask);

	/* Release coldboot lock */
	spin_unlock(&coldboot_lock);

	/* Restore MIE CSR */
	csr_write(CSR_MIE, saved_mie);

	/*
	 * The wait for coldboot is common for both warm startup and
	 * warm resume path so clearing IPI here would result in losing
	 * an IPI in warm resume path.
	 *
	 * Also, the sbi_platform_ipi_init() called from sbi_ipi_init()
	 * will automatically clear IPI for current HART.
	 */
}

static void wake_coldboot_harts(struct sbi_scratch *scratch, u32 hartid)
{
	/* Mark coldboot done */
	__smp_store_release(&coldboot_done, 1);

	/* Acquire coldboot lock */
	spin_lock(&coldboot_lock);

	/* Send an IPI to all HARTs waiting for coldboot */
	for (u32 i = 0; i <= sbi_scratch_last_hartid(); i++) {
		if ((i != hartid) &&
		    sbi_hartmask_test_hart(i, &coldboot_wait_hmask))
			sbi_ipi_raw_send(i);
	}

	/* Release coldboot lock */
	spin_unlock(&coldboot_lock);
}

static unsigned long init_count_offset;

static void __noreturn init_coldboot(struct sbi_scratch *scratch, u32 hartid)
{
	int rc;
	unsigned long *init_count;
	const struct sbi_platform *plat = sbi_platform_ptr(scratch);

	/* Note: This has to be first thing in coldboot init sequence */
	rc = sbi_scratch_init(scratch);
	if (rc)
		sbi_hart_hang();

	/* Note: This has to be second thing in coldboot init sequence */
	rc = sbi_domain_init(scratch, hartid);
	if (rc)
		sbi_hart_hang();

	init_count_offset = sbi_scratch_alloc_offset(__SIZEOF_POINTER__);
	if (!init_count_offset)
		sbi_hart_hang();

	rc = sbi_hsm_init(scratch, hartid, TRUE);
	if (rc)
		sbi_hart_hang();

	rc = sbi_platform_early_init(plat, TRUE);
	if (rc)
		sbi_hart_hang();

	rc = sbi_hart_init(scratch, TRUE);
	if (rc)
		sbi_hart_hang();

	rc = sbi_console_init(scratch);
	if (rc)
		sbi_hart_hang();

	rc = sbi_pmu_init(scratch, TRUE);
	if (rc)
		sbi_hart_hang();

	sbi_boot_print_banner(scratch);

	rc = sbi_irqchip_init(scratch, TRUE);
	if (rc) {
		sbi_printf("%s: irqchip init failed (error %d)\n",
			   __func__, rc);
		sbi_hart_hang();
	}

	rc = sbi_ipi_init(scratch, TRUE);
	if (rc) {
		sbi_printf("%s: ipi init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}

	rc = sbi_tlb_init(scratch, TRUE);
	if (rc) {
		sbi_printf("%s: tlb init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}

	rc = sbi_timer_init(scratch, TRUE);
	if (rc) {
		sbi_printf("%s: timer init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}

    //sbi_timer_event_start(100000);

	rc = sbi_ecall_init();
	if (rc) {
		sbi_printf("%s: ecall init failed (error %d)\n", __func__, rc);
		sbi_hart_hang();
	}

	/*
	 * Note: Finalize domains after HSM initialization so that we
	 * can startup non-root domains.
	 * Note: Finalize domains before HART PMP configuration so
	 * that we use correct domain for configuring PMP.
	 */
	rc = sbi_domain_finalize(scratch, hartid);
	if (rc) {
		sbi_printf("%s: domain finalize failed (error %d)\n",
			   __func__, rc);
		sbi_hart_hang();
	}

	rc = sbi_hart_pmp_configure(scratch);
	if (rc) {
		sbi_printf("%s: PMP configure failed (error %d)\n",
			   __func__, rc);
		sbi_hart_hang();
	}

	/*
	 * Note: Platform final initialization should be last so that
	 * it sees correct domain assignment and PMP configuration.
	 */
	rc = sbi_platform_final_init(plat, TRUE);
	if (rc) {
		sbi_printf("%s: platform final init failed (error %d)\n",
			   __func__, rc);
		sbi_hart_hang();
	}

	sbi_boot_print_general(scratch);

	sbi_boot_print_domains(scratch);

	sbi_boot_print_hart(scratch, hartid);

	wake_coldboot_harts(scratch, hartid);

	init_count = sbi_scratch_offset_ptr(scratch, init_count_offset);
	(*init_count)++;

	sbi_hsm_prepare_next_jump(scratch, hartid);

#ifdef LAUNCH_TYCHE
    sbi_printf("\n-----------------FW_JUMP_ADDR: %lx to mode %ld, TYCHE_SM_START_CONTENT %x, TYCHE_SM_START_ADDR %p -------------------\n", scratch->next_addr, scratch->next_mode, tyche_sm_bin, &tyche_sm_bin); 
	
   tyche_loader_resp* tlr = parse_and_load_elf(&tyche_sm_bin, (void*)TYCHE_LOAD_ADDRESS);

	sbi_printf("\nARGS for TYCHE_SM: hartid: %d , arg1: %lx, next_addr: %lx, next_mode: %ld \n", hartid, scratch->next_arg1, scratch->next_addr, scratch->next_mode);

	void* tyche_start = (void*)tlr->tyche_entry;

    struct tyche_manifest* manifest = (struct tyche_manifest*) TYCHE_MANIFEST_ADDRESS; 

    manifest->next_arg1 = scratch->next_arg1;
    manifest->next_addr = scratch->next_addr;
    manifest->next_mode = scratch->next_mode;
    manifest->coldboot_hartid = hartid;
    manifest->num_harts = sbi_platform_hart_count(plat);  

    sbi_printf("\nTyche Manifest: num_harts: %ld", manifest->num_harts);

    //Neelu: Todo start: Send IPIs to the other cores. Basically mimic an SBI_HSM_HART_START call. 
    
    //To mimic that - it needs the following reg state: For instance,  
    //a0: 3 a1: 80201066 a2: 17efae370 a3: 0 a4: 0 a5: 0 a6: 0 a7: 48534d 
    //Here, hartid = 3, a1 contains the next_addr (should be in Tyche for us),
    //a2 is arg1 for next stage - I think we can assume it to be 0 here because Tyche doesn't expect this and then Tyche can set it for Linux as expected when Linux makes the SBI call. 
    //a7 is ext_id (HSM extension here) and a6 is func_id (0 for HART_START) 
    //a3/a4/a5 are don't care - better to keep them 0 Ig. 
   
    for(int i = 0; i < manifest->num_harts; i++) {
        sbi_printf("\nMaking an ecall for hartid: %d\n",i);
        if(i != hartid) {
            __asm__ __volatile__ (
                "mv a0, %[sa0]\n\t" 
                "mv a1, %[sa1]\n\t"
                "mv a2, zero\n\t"
                "mv a3, zero\n\t"
                "mv a4, zero\n\t"
                "mv a5, zero\n\t"
                "mv a6, zero\n\t"
                "li a7, 0x48534d\n\t"
                //Todo - store mepc? I guess it shouldn't be needed? The same for MPP - it should automatically be m-mode I suppose 
                "ecall\n\t" 

                :
                : [sa0]"r"(i), [sa1]"r"(tlr->tyche_entry) 
                : "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
            );
            
            sbi_printf("\nReturned from ecall for hartid: %d\n",i);
        }
    } 

    //Make sure it returns here - so MEPC and MPP should be appropriate. 
    //There's a check in sbi_hsm_hart_start which prevents starting the harts in M-mode! I will comment this check.
    
    //Then make sure that mswi doesn't get converted to sswi? 
    //Yes because mswi wakes up the hart by putting START_PENDING in the state and sending an interrupt - the start pending then leads to the completion of the remaining hart_init process and it eventually jumps to the "next_addr" in the scratch memory. 

    //Make sure to jump to Tyche for warmboot harts - to be changed in init_warmboot (replace sbi_hart_switch_mode). 

    //Wait until all the harts reach STARTED state 
    //STARTED is represented by 0x0 - so when all the harts are started, the sum will be 0.
    int num_harts_started = 0; // WTF? 
    while(num_harts_started != 0) {
		sbi_printf(" I SHOULD NEVER EXECUTE! ");
        num_harts_started = 0;
        for(int i = 0; i < manifest->num_harts; i++) {
            num_harts_started += sbi_hsm_hart_get_state(sbi_domain_thishart_ptr(), i);
        }
    } 

    //Neelu: Todo end  

    //sbi_timer_event_start(100000);

	sbi_printf("\nArgs to Anchor: tyche_start_addr: %x, region_size: %lx, entry_addr: %lx, hartid: %x, manifest_addr: %p", TYCHE_LOAD_ADDRESS, manifest->next_addr - TYCHE_LOAD_ADDRESS - 1, tlr->tyche_entry, hartid, manifest);

	// enter_anchor((unsigned long)TYCHE_LOAD_ADDRESS, manifest->next_addr - TYCHE_LOAD_ADDRESS - 1, tlr->tyche_entry, hartid, manifest);

	((void (*) (unsigned long, struct tyche_manifest*))tyche_start)(hartid, manifest);

#else 
	sbi_hart_switch_mode(hartid, scratch->next_arg1, scratch->next_addr, scratch->next_mode, FALSE);
#endif 

    __builtin_unreachable();
}

static void init_warm_startup(struct sbi_scratch *scratch, u32 hartid)
{
    sbi_printf("\n[%s] for hartid: %d\n",__func__,hartid);
	int rc;
	unsigned long *init_count;
	const struct sbi_platform *plat = sbi_platform_ptr(scratch);

	if (!init_count_offset)
		sbi_hart_hang();

	rc = sbi_hsm_init(scratch, hartid, FALSE);
	if (rc)
		sbi_hart_hang();

	rc = sbi_platform_early_init(plat, FALSE);
	if (rc)
		sbi_hart_hang();

	rc = sbi_hart_init(scratch, FALSE);
	if (rc)
		sbi_hart_hang();

	rc = sbi_pmu_init(scratch, FALSE);
	if (rc)
		sbi_hart_hang();

	rc = sbi_irqchip_init(scratch, FALSE);
	if (rc)
		sbi_hart_hang();

	rc = sbi_ipi_init(scratch, FALSE);
	if (rc)
		sbi_hart_hang();

	rc = sbi_tlb_init(scratch, FALSE);
	if (rc)
		sbi_hart_hang();

	rc = sbi_timer_init(scratch, FALSE);
	if (rc)
		sbi_hart_hang();

    //sbi_timer_event_start(100000);

    rc = sbi_hart_pmp_configure(scratch);
	if (rc)
		sbi_hart_hang();

	rc = sbi_platform_final_init(plat, FALSE);
	if (rc)
		sbi_hart_hang();

	init_count = sbi_scratch_offset_ptr(scratch, init_count_offset);
	(*init_count)++;

	sbi_hsm_prepare_next_jump(scratch, hartid);
}

static void init_warm_resume(struct sbi_scratch *scratch)
{
	int rc;

	sbi_hsm_hart_resume_start(scratch);

	rc = sbi_hart_reinit(scratch);
	if (rc)
		sbi_hart_hang();

	rc = sbi_hart_pmp_configure(scratch);
	if (rc)
		sbi_hart_hang();

	sbi_hsm_hart_resume_finish(scratch);
}

static void __noreturn init_warmboot(struct sbi_scratch *scratch, u32 hartid)
{
	int hstate;
    
    sbi_printf("\n[%s] for hartid: %d\n",__func__,hartid);

	wait_for_coldboot(scratch, hartid);

    sbi_printf("\n[%s] Done waiting for coldboot for hartid: %d\n",__func__,hartid);

	hstate = sbi_hsm_hart_get_state(sbi_domain_thishart_ptr(), hartid);
	if (hstate < 0)
		sbi_hart_hang();

	if (hstate == SBI_HSM_STATE_SUSPENDED)
		init_warm_resume(scratch);
	else
		init_warm_startup(scratch, hartid);

#ifdef LAUNCH_TYCHE
    void* tyche_start = (void*)scratch->next_addr;
    
    //sbi_timer_event_start(100000);
    //In the following - manifest_addr is not really needed - Tyche won't do anything with it 
    ((void (*) (unsigned long, struct tyche_manifest*))tyche_start)(hartid,(struct tyche_manifest*)TYCHE_MANIFEST_ADDRESS);
	
	// struct tyche_manifest* manifest = (struct tyche_manifest*) TYCHE_MANIFEST_ADDRESS; 

	// enter_anchor((unsigned long)TYCHE_LOAD_ADDRESS, manifest->next_addr - TYCHE_LOAD_ADDRESS - 1, scratch->next_addr, hartid, (struct tyche_manifest*)TYCHE_MANIFEST_ADDRESS);
#else
	sbi_hart_switch_mode(hartid, scratch->next_arg1,
			     scratch->next_addr,
			     scratch->next_mode, FALSE);
#endif
    __builtin_unreachable();
}

static atomic_t coldboot_lottery = ATOMIC_INITIALIZER(0);

/**
 * Initialize OpenSBI library for current HART and jump to next
 * booting stage.
 *
 * The function expects following:
 * 1. The 'mscratch' CSR is pointing to sbi_scratch of current HART
 * 2. Stack pointer (SP) is setup for current HART
 * 3. Interrupts are disabled in MSTATUS CSR
 * 4. All interrupts are disabled in MIE CSR
 *
 * @param scratch pointer to sbi_scratch of current HART
 */
void __noreturn sbi_init(struct sbi_scratch *scratch)
{
	bool next_mode_supported	= FALSE;
	bool coldboot			= FALSE;
	u32 hartid			= current_hartid();
	const struct sbi_platform *plat = sbi_platform_ptr(scratch);

	if ((SBI_HARTMASK_MAX_BITS <= hartid) ||
	    sbi_platform_hart_invalid(plat, hartid))
		sbi_hart_hang();

	switch (scratch->next_mode) {
	case PRV_M:
		next_mode_supported = TRUE;
		break;
	case PRV_S:
		if (misa_extension('S'))
			next_mode_supported = TRUE;
		break;
	case PRV_U:
		if (misa_extension('U'))
			next_mode_supported = TRUE;
		break;
	default:
		sbi_hart_hang();
	}

	/*
	 * Only the HART supporting privilege mode specified in the
	 * scratch->next_mode should be allowed to become the coldboot
	 * HART because the coldboot HART will be directly jumping to
	 * the next booting stage.
	 *
	 * We use a lottery mechanism to select coldboot HART among
	 * HARTs which satisfy above condition.
	 */

	if (next_mode_supported && atomic_xchg(&coldboot_lottery, 1) == 0)
		coldboot = TRUE;

	/*
	 * Do platform specific nascent (very early) initialization so
	 * that platform can initialize platform specific per-HART CSRs
	 * or per-HART devices.
	 */
	if (sbi_platform_nascent_init(plat))
		sbi_hart_hang();

	if (coldboot)
		init_coldboot(scratch, hartid);
	else
		init_warmboot(scratch, hartid);
}

unsigned long sbi_init_count(u32 hartid)
{
	struct sbi_scratch *scratch;
	unsigned long *init_count;

	if (!init_count_offset)
		return 0;

	scratch = sbi_hartid_to_scratch(hartid);
	if (!scratch)
		return 0;

	init_count = sbi_scratch_offset_ptr(scratch, init_count_offset);

	return *init_count;
}

/**
 * Exit OpenSBI library for current HART and stop HART
 *
 * The function expects following:
 * 1. The 'mscratch' CSR is pointing to sbi_scratch of current HART
 * 2. Stack pointer (SP) is setup for current HART
 *
 * @param scratch pointer to sbi_scratch of current HART
 */
void __noreturn sbi_exit(struct sbi_scratch *scratch)
{
	u32 hartid			= current_hartid();
	const struct sbi_platform *plat = sbi_platform_ptr(scratch);

	if (sbi_platform_hart_invalid(plat, hartid))
		sbi_hart_hang();

	sbi_platform_early_exit(plat);

	sbi_pmu_exit(scratch);

	sbi_timer_exit(scratch);

	sbi_ipi_exit(scratch);

	sbi_irqchip_exit(scratch);

	sbi_platform_final_exit(plat);

	sbi_hsm_exit(scratch);
}
