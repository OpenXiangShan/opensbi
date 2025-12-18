#include <sbi/sbi_bitmap.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_bitops.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_types.h>
#include <sbi/sbi_heap.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_domain.h>
#include <sbi/sbi_scratch.h>
#include <libfdt_env.h>
#include <libfdt.h>

#include <kmh_ecall_container.h>

unsigned long *pfn_bitmap;

void sbi_bitmap_set(unsigned long *bitmap, unsigned long pfn) 
{
    unsigned long word_offset = pfn / BITS_PER_LONG;
    unsigned long bit_offset = pfn % BITS_PER_LONG;
    unsigned long mask = 1UL << bit_offset;
    bitmap[word_offset] |= mask;
}

void sbi_bitmap_clear(unsigned long *bitmap, unsigned long pfn) 
{
    unsigned long word_offset = pfn / BITS_PER_LONG;
    unsigned long bit_offset = pfn % BITS_PER_LONG;
    unsigned long mask = 1UL << bit_offset;
    bitmap[word_offset] &= ~mask;
}

int sbi_container_init(void)
{
    if(sbi_bitmap_init()!= SBI_SUCCESS){
        sbi_printf("ERROR: Failed to init bitmap!\n");
        return SBI_ERR_FAILED;
    }
    if(sbi_bclear_set()!= SBI_SUCCESS){
        sbi_printf("ERROR: Failed to set bclear!\n");
        return SBI_ERR_FAILED;
    }
    return SBI_SUCCESS;
}

int sbi_bclear_set(void)
{
    csr_set(CSR_MBMC, 0x2);
    return SBI_SUCCESS;
}

int sbi_bitmap_init(void)
{
    size_t bitmap_size;
    unsigned long mbmc_val = 0;
    
    // 1. 分配并初始化 bitmap
    bitmap_size = BITS_TO_LONGS(MAX_PFN) * sizeof(unsigned long);

    pfn_bitmap = sbi_malloc(bitmap_size);

    if (!pfn_bitmap) {
        sbi_printf("ERROR: Failed to allocate %lu bytes for PFN bitmap!\n", bitmap_size);
        return SBI_ERR_FAILED;
    }
    memset(pfn_bitmap, 0, bitmap_size);
    
    // 2. 获取 bitmap 的物理地址（OpenSBI 运行在 M 模式，VA=PA）
    uintptr_t bitmap_phys_addr = (uintptr_t)pfn_bitmap;
    
    // 3. 构建 MBMC CSR 的值
    // CMODE=0, BCLEAR=0, BME=1, RSV=0, BMA=bitmap物理地址
    mbmc_val = 0;  // 初始化为0
    
    // 设置 BME=1（启用 bitmap 机制）
    mbmc_val |= (1UL << MBMC_BME_BIT);
    
    // 设置 BMA（bitmap 物理地址，确保地址对齐到64字节）
    if (bitmap_phys_addr & 0x3F) {
        sbi_printf("WARNING: Bitmap address not 64-byte aligned: 0x%lx\n", 
                   bitmap_phys_addr);
        bitmap_phys_addr = (bitmap_phys_addr + 0x3F) & ~0x3F;  // 向上对齐
    }
    mbmc_val |= (bitmap_phys_addr & MBMC_BMA_MASK);
    
    // 确保 RSV=0（保留位清零）
    mbmc_val &= ~MBMC_RSV_MASK;
    
    // 确保 CMODE=0, BCLEAR=0（这些位已经是0）
    
    // 4. 写入 MBMC CSR
    csr_write(CSR_MBMC, mbmc_val);
    
    // 5. 验证写入结果（可选）
    unsigned long readback = csr_read(CSR_MBMC);
    if (readback != mbmc_val) {
        sbi_printf("ERROR: MBMC CSR write failed! Expected: 0x%lx, Got: 0x%lx\n",
                   mbmc_val, readback);
        return SBI_ERR_FAILED;
    }
    
    return SBI_SUCCESS;
}
