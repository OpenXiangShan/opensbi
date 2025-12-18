#ifndef __KMH_ECALL_CONTAINER_H__
#define __KMH_ECALL_CONTAINER_H__
#include <sbi/sbi_bitmap.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_scratch.h>
#include <sbi/riscv_asm.h>
#include <sbi/sbi_types.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_platform.h>
#include <sbi_utils/fdt/fdt_helper.h>

#include <kmh_container.h>

#define SBI_EXT_VENDOR_SET_PFN                   0x00
#define SBI_EXT_VENDOR_CLEAR_PFN                 0x01
#define SBI_EXT_VENDOR_SET_CSR_MBMC_CMODE        0x02
#define SBI_EXT_VENDOR_CLEAR_CSR_MBMC_CMODE      0x03

/* 定义 MBMC CSR 的 cmode 位偏移 */
#define CSR_MBMC            0xBC2  // NEMU中MBMC地址
#define MBMC_CMODE_BIT      0      // MBMC中CMODE是第0位

/* 假设全局的 PFN bitmap（需自行定义和初始化） */
#define CONFIG_RAM_SIZE (1UL << 24)
#define MAX_PFN  (CONFIG_RAM_SIZE / PAGE_SIZE)  // 根据实际需求定义
// static spinlock_t bitmap_lock;  // 多核安全锁

extern unsigned long *pfn_bitmap;

int bitmap_set_pfn(unsigned long pfn);
int bitmap_clear_pfn(unsigned long pfn);
int set_mbmc_cmode(void);
int clear_mbmc_cmode(void);
int kmh_v2_vendor_ext_provider(const struct sbi_platform *plat,
                                long funcid,
                                struct sbi_trap_regs *regs,
                                struct sbi_ecall_return *out);

#endif