#ifndef __SBI_ECC_POLLING_H__
#define __SBI_ECC_POLLING_H__

#include <sbi/sbi_types.h>
#include <sbi/riscv_io.h>

/* === BEU Register Addresses (XiangShan Specific) === */
#define BEU_BASE            0x38010000UL
#define BEU_CAUSE           (BEU_BASE + 0x00)   // RW, event ID
#define BEU_VALUE           (BEU_BASE + 0x08)   // RO, fault PA
#define BEU_ACCRUED_INTR    (BEU_BASE + 0x20)   // RW, status
#define BEU_LOCAL_INTR      (BEU_BASE + 0x28)   // RW, enable mask

/* === ECC Control Unit === */
#define CTRLUNIT_BASE_ADDR  0x38022000UL        // Adjust if needed
#define ECCCTL_OFFSET       0x00
#define ECCEID_OFFSET       0x08
#define ECCMASK_OFFSET      0x10

/* Read/Write helpers */
static inline uint64_t ecc_read(uint64_t addr)
{
    return readq((volatile uint64_t *)addr); //*(volatile uint64_t *)addr;
}

static inline void ecc_write(uint64_t addr, uint64_t val)
{
	writeq(val, (volatile uint64_t *)addr);
   // *(volatile uint64_t *)addr = val;
}

/* Clear all BEU status (safe, no interrupt) */
static inline void beu_clear_status(void)
{
    ecc_write(BEU_ACCRUED_INTR, 0);
    ecc_write(BEU_CAUSE, 0);
}

/* Check if ECC error occurred (polling) */
static inline bool beu_has_error(void)
{
    return ecc_read(BEU_CAUSE) != 0;
}

#endif /* __SBI_ECC_POLLING_H__ */
