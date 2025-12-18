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
#include <sbi/sbi_console.h>
#include <sbi/sbi_platform.h>
#include <sbi_utils/fdt/fdt_helper.h>

#include <platform_override.h>
#include <kmh_container.h>
#include <kmh_ecall_container.h>

static spinlock_t bitmap_lock;  // 多核安全锁

/* 接口1：设置 PFN 对应的 bitmap 位 */
int bitmap_set_pfn(unsigned long pfn) {
    if (pfn >= CONFIG_RAM_SIZE + 0x80000000 || !pfn_bitmap)
        return SBI_ERR_INVALID_PARAM;
    spin_lock(&bitmap_lock);
    sbi_bitmap_set(pfn_bitmap, pfn);
    if(sbi_bclear_set()!= SBI_SUCCESS){
        sbi_printf("ERROR: Failed to set bclear!\n");
        return SBI_ERR_FAILED;
    }
    spin_unlock(&bitmap_lock);
    return SBI_SUCCESS;
}

/* 接口2：清除 PFN 对应的 bitmap 位 */
int bitmap_clear_pfn(unsigned long pfn) {
    if (pfn >= CONFIG_RAM_SIZE + 0x80000000 || !pfn_bitmap)
        return SBI_ERR_INVALID_PARAM;
    spin_lock(&bitmap_lock);
    sbi_bitmap_clear(pfn_bitmap, pfn);
    if(sbi_bclear_set()!= SBI_SUCCESS){
        sbi_printf("ERROR: Failed to set bclear!\n");
        return SBI_ERR_FAILED;
    }
    spin_unlock(&bitmap_lock);
    return SBI_SUCCESS;
}

/* 接口3：设置 MBMC CSR 的 cmode 位 */
int set_mbmc_cmode(void) {
    csr_set(CSR_MBMC, 0x1);
    asm volatile ("sfence.vma" ::: "memory");
    return SBI_SUCCESS; 
}

/* 接口4：清除 MBMC CSR 的 cmode 位 */
int clear_mbmc_cmode(void) {
    csr_clear(CSR_MBMC, 0x1);
    asm volatile ("sfence.vma" ::: "memory");
    return SBI_SUCCESS;
}

/* SBI 调用分发函数 */
int kmh_v2_vendor_ext_provider(const struct sbi_platform *plat,
                                long funcid,
                                struct sbi_trap_regs *regs,
                                struct sbi_ecall_return *out)
{
    int ret = SBI_ERR_NOT_SUPPORTED;

    switch (funcid) {
    case SBI_EXT_VENDOR_SET_PFN:
        ret = bitmap_set_pfn(regs->a0);  // pfn in a0
        break;
    case SBI_EXT_VENDOR_CLEAR_PFN:
        ret = bitmap_clear_pfn(regs->a0);
        break;
    case SBI_EXT_VENDOR_SET_CSR_MBMC_CMODE:
        ret = set_mbmc_cmode();
        break;
    case SBI_EXT_VENDOR_CLEAR_CSR_MBMC_CMODE:
        ret = clear_mbmc_cmode();
        break;
    default:
        ret = SBI_ERR_NOT_SUPPORTED;
    }

    out->value = ret;
    return 0;  // 注意：返回 0 表示 handler 成功执行，错误码在 out->value
}

/* mvendorid = 0x86F (i.e., decimal 2159), and the SBI vendor extension ID for the Xiangshan platform is 0x0900086F */
static inline unsigned long sbi_ecall_vendor_id(void)
{
    return SBI_EXT_VENDOR_START +
           (csr_read(CSR_MVENDORID) &
            (SBI_EXT_VENDOR_END - SBI_EXT_VENDOR_START));
}

static int sbi_ecall_vendor_handler(unsigned long extid, unsigned long funcid,
                                    struct sbi_trap_regs *regs,
                                    struct sbi_ecall_return *out)
{
    return kmh_v2_vendor_ext_provider(sbi_platform_thishart_ptr(),
                                            funcid, regs, out);
}

struct sbi_ecall_extension ecall_vendor;

static int sbi_ecall_vendor_register_extensions(void)
{
    unsigned long extid = sbi_ecall_vendor_id();

    // 首次调用时设置真实 EID（可加防重入）
    ecall_vendor.extid_start = extid;
    ecall_vendor.extid_end   = extid;
    return sbi_ecall_register_extension(&ecall_vendor);
}

/* 全局注册入口（被 SBI 框架扫描） */
struct sbi_ecall_extension ecall_vendor = {
    .extid_start = SBI_EXT_VENDOR_START,
    .extid_end   = SBI_EXT_VENDOR_END,
    .register_extensions = sbi_ecall_vendor_register_extensions,
    .handle = sbi_ecall_vendor_handler,
};