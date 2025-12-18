#ifndef __KMH_CONTAINER_H__
#define __KMH_CONTAINER_H__
#include <sbi/sbi_bitmap.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_bitops.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_types.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_ecall_interface.h>
#include <libfdt.h>
#include <sbi/sbi_domain.h>

#define CONFIG_RAM_SIZE (1UL << 24)
#define MAX_PFN  (CONFIG_RAM_SIZE / PAGE_SIZE)
//static spinlock_t bitmap_lock;  // 多核安全锁

/* 定义 MBMC CSR 的 cmode 位偏移 */
#define CSR_MBMC            0xBC2  // NEMU中MBMC地址
// MBMC CSR 位域定义
#define MBMC_CMODE_BIT   0
#define MBMC_BCLEAR_BIT  1
#define MBMC_BME_BIT     2
#define MBMC_RSV_BIT     3  // RSV[5:3] 共3位，从第3位开始
#define MBMC_BMA_BIT     6  // BMA[63:6] 从第6位开始

#define MBMC_CMODE_MASK   (1UL << MBMC_CMODE_BIT)
#define MBMC_BCLEAR_MASK  (1UL << MBMC_BCLEAR_BIT)
#define MBMC_BME_MASK     (1UL << MBMC_BME_BIT)
#define MBMC_RSV_MASK     (0x7UL << MBMC_RSV_BIT)  // 3位掩码
#define MBMC_BMA_MASK     (0xFFFFFFFFFFFFFFC0UL)   // 低6位清零的掩码

int sbi_bclear_set(void);
int sbi_bitmap_init(void);
int sbi_container_init(void);
void sbi_bitmap_set(unsigned long *bitmap, unsigned long pfn);
void sbi_bitmap_clear(unsigned long *bitmap, unsigned long pfn);

#endif