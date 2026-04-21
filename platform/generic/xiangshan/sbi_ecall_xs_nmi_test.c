#include "sbi_ecc_polling.h"

#include <sbi/sbi_timer.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_trap.h>
#include <sbi/riscv_io.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_hart.h>

// Note: BEU reports injected PA at fixed offset (e.g., buf VA 0x80048000 → PA 0x80049d30)
// This is normal behavior on XiangShan.
/*
 * Note on BEU behavior:
 * - Tag ECC always reports PA=0x80049d30 (fixed placeholder)
 * - Data ECC may report PA in 0x80043xxx or 0x80049xxx ranges
 *   due to internal bank mapping. This is normal.
 * - Use handle_count and CAUSE=0x1 as success indicators.
 */

void cbo_cache_inval(void *addr);
void cbo_cache_flush(void *addr);
void init_clear_ecc_injection(void);
void test_data_Decc_error_all_banks(void);

#define BANK_COUNT 8
volatile uint64_t pa = 0;
volatile uint64_t cause = 0;
volatile uint64_t acc_intr = 0;
volatile uint64_t mncause;
volatile uint64_t mnepc;
volatile int handle_count = 0;

/* ----- 中断管理辅助函数 ----- */
static volatile unsigned long saved_mie;

static inline void disable_interrupts(void)
{
    // 保存当前 MIE 状态
//	 return ; 
    saved_mie = csr_read(CSR_MIE);
    // 清除机器定时器中断和外部中断 (不关软件中断，因为可能用于IPI)
    csr_clear(CSR_MIE, MIP_MTIP | MIP_MEIP | MIP_MSIP );
    // 确保写入生效
    asm volatile("fence iorw, iorw" ::: "memory");
}

static inline void enable_interrupts(void)
{
    // 恢复原来的 MIE
//	 return ; 
    csr_write(CSR_MIE, saved_mie);
    asm volatile("fence iorw, iorw" ::: "memory");
}

/* ----- 自旋延时 (不使用定时器中断) ----- */
static void spin_delay_us(unsigned long us)
{
    // 粗略延时：假设 CPU 频率 1GHz，一个空循环约 2-4 个周期，此处简单循环
    // 实际可根据需要调整，这里使用 volatile 防止优化
    volatile unsigned long loops = us * 200; // 经验值，可调整
    while (loops--)
        asm volatile("" ::: "memory");
}

static void spin_delay_ms(unsigned long ms)
{
    for (unsigned long i = 0; i < ms; i++)
        spin_delay_us(1000);
}

/*
volatile uint64_t  save_pa[100];
volatile uint64_t  save_cause[100];
void Get_print_event(void)
{

//  if (handle_count < 10)
//	  return ;

  for ( int i=0; i < handle_count; i ++)
  sbi_printf("✅handle_count =%d ECC Triggered! CAUSE=0x%lx, PA=0x%lx, acc_intr=0x%lx, mncause=0x%lx, mnepc=0x%lx, bit[5:3]=%u\n", i, save_cause[i], save_pa[i], acc_intr, mncause, mnepc, (unsigned int)(((save_pa[i] & 0xFF) >> 3) & 0x7));
  cause = 0;
  pa    = 0;  
}*/

void Get_print_event(void)
{
  sbi_printf("✅handle_count =%d ECC Triggered! CAUSE=0x%lx, PA=0x%lx, acc_intr=0x%lx, mncause=0x%lx, mnepc=0x%lx, bit[5:3]=%u\n", handle_count, cause, pa, acc_intr, mncause, mnepc, (unsigned int)(((pa & 0xFF) >> 3) & 0x7));
  cause = 0;
  pa    = 0;
}

__attribute__((aligned(64), section(".ecc_test"))) 
__attribute__((aligned(4096))) uint64_t ecc_test_buf[32];
volatile static int bank = 0;
volatile static int indexa = 0;
// For BANK_COUNT = 8, 64-bit ECCMASK values (random but valid)
const uint64_t eccmask_valuesA[8] = {
    0x1A2B3C4D5E6F7890ULL,  // Bank 0
    0xF0E1D2C3B4A59687ULL,  // Bank 1
    0x00FF00FF00FF00FFULL,  // Bank 2
    0x55AA55AA55AA55AAULL,  // Bank 3
    0xC0C0C0C0C0C0C0C0ULL,  // Bank 4
    0x123456789ABCDEF0ULL,  // Bank 5
    0xFEDCBA9876543210ULL,  // Bank 6
    0xAAAAAAAAAAAAAAAAULL   // Bank 7
};

// For BANK_COUNT = 8, new random 64-bit ECCMASK values
const uint64_t eccmask_valuesC[8] = {
    0x2D3E4F5A6B7C8D9EULL,  // Bank 0
    0xE9D8C7B6A5948372ULL,  // Bank 1
    0x1122334455667788ULL,  // Bank 2
    0x99AABBCCDDEEFF00ULL,  // Bank 3
    0x0F1E2D3C4B5A6978ULL,  // Bank 4
    0x876543210FEDCBA9ULL,  // Bank 5
    0xFEDCBA9876543210ULL,  // Bank 6 (保留一个之前的值作为示例)
    0xBBBBCCCCDDDDEEEFULL   // Bank 7
};

const uint64_t eccmask_valuesD[8] = {
    0x3f3f3f3f3f3f3f3fULL,  // Bank 0
    0x3f3f3f3f3f3f3f3fULL,
    0x3f3f3f3f3f3f3f3fULL,
    0x3f3f3f3f3f3f3f3fULL,
    0x3f3f3f3f3f3f3f3fULL,
    0x3f3f3f3f3f3f3f3fULL,
    0x3f3f3f3f3f3f3f3fULL,
    0x3f3f3f3f3f3f3f3fULL
};

void test_data_ecc_error_all_banks(void)
{

    // 1. 选安全地址
    extern char _bss_end[];
    extern char _fw_end[];

    uint64_t *  addr = (uint64_t *) (((uintptr_t)_bss_end + 0x100 + bank*4) & ~0x3FUL); // align to 64B

    if (addr + 64 > (uint64_t *) _fw_end) {
//        sbi_printf("No safe space: bss_end=0x%ld, fw_end=0x%ld\n", bss_end, fw_end);
        return;
    }

   addr[0] = 0xa55bbaa789ABCD0ULL;
 
 //   uint64_t ecc_test_buf[32] __attribute__((aligned(64)));
    for (int i = 0; i < 32; i++) {
        ecc_test_buf[i] = 0xDEADBEEFCAFEBABEULL + i;
    }

//	sbi_printf("BSS end: 0x%p, addr=0x%p, &ecc_test_buf[%d]=0x%p\n", &_bss_end, addr, bank*4, &ecc_test_buf[bank*4]);
//    sbi_printf("=== Polling-based Data ECC Test ===\n");

    // Step 1: 清理状态
    disable_interrupts();
    init_clear_ecc_injection();
   // ecc_write(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET, 0);
    spin_delay_ms(10);

    ecc_write(BEU_LOCAL_INTR, 0xFF);  // 禁用所有 BEU 中断
			
    spin_delay_ms(20); 

    asm volatile("fence iorw, iorw" ::: "memory");

    //  for (int banka = 0; banka < BANK_COUNT; banka++) {
  //      sbi_printf("Testing Bank %d...\n", bank);

        // Step 2: 配置 ECCMASK
        ecc_write(CTRLUNIT_BASE_ADDR + ECCMASK_OFFSET + bank * 8, eccmask_valuesA[bank] /*+ handle_count + 7*/);

	asm volatile("fence w, w" ::: "memory");

        // Step 3: 配置 ECCCTL (one-hot mask, PST=0 for single shot)
        uint64_t ctl = (1ULL << 0) |   // ESE=1
                       (0ULL << 1) |   // PST=0 (single trigger)
                       (1ULL << 2) |   // EDE=1 (wait until ECCEID=0)
                       (1ULL << 3) |   // CMP=1 (data)
                       (1ULL << (4 + bank)); // Enable bank 'bank'

        ecc_write(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET, 10); // Short delay
        ecc_write(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET, ctl);
	asm volatile("fence w, w" ::: "memory");

// 	sbi_printf("Bank %d, set_ctl=0x%lx,back_ctl=0x%lx. 1\n", bank, ctl, ecc_read(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET));

    //   }
    

   //  sbi_printf("Testing Bank %d.., readback_ecceid=0x%lx. 2\n", bank, ecc_read(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET));

    // Step 4: 强制 Cache Miss
    	
      for (int loop =0; loop < 1; loop++) { 
//	 cbo_cache_flush(&ecc_test_buf[bank]);
	
        // Step 5: 触发访问
 
         volatile uint64_t dummy = ecc_test_buf[indexa]; //addr
         (void)dummy;  
	
//	asm volatile("fence iorw, iorw" ::: "memory");	 
	 // sbi_printf("Bank %d,back_eid=0x%lx.\n", bank, ecc_read(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET));
        // Step 7: 清理，准备下一轮
      }
      asm volatile("fence iorw, iorw" ::: "memory");
      enable_interrupts();
      bank++;
      bank = (bank)%BANK_COUNT;
      indexa++;
      indexa = (indexa)%32;


    sbi_printf("=== Polling data ecc Test Completed : indexa=%d\n", indexa);
}

void  Init_Data_Ecc(void)
{
	disable_interrupts();
	init_clear_ecc_injection();
	spin_delay_ms(10);

	ecc_write(BEU_LOCAL_INTR, 0xFF);  // 禁用所有 BEU 中断
	spin_delay_ms(20);
	asm volatile("fence iorw, iorw" ::: "memory");

	for (int bank = 0; bank < BANK_COUNT; bank++) {
		ecc_write(CTRLUNIT_BASE_ADDR + ECCMASK_OFFSET + bank * 8, eccmask_valuesA[bank] );

		asm volatile("fence w, w" ::: "memory");

		  // Step 3: 配置 ECCCTL (one-hot mask, PST=0 for single shot)
		uint64_t ctl = (1ULL << 0) |   // ESE=1
					   (0ULL << 1) |   // PST=0 (single trigger)
					   (1ULL << 2) |   // EDE=1 (wait until ECCEID=0)
					   (1ULL << 3) |   // CMP=1 (data)
					   (1ULL << (4 + bank)); // Enable bank 'bank'

		ecc_write(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET, 10); // Short delay
		ecc_write(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET, ctl);
		asm volatile("fence w, w" ::: "memory");
	}

	asm volatile("fence iorw, iorw" ::: "memory");
	enable_interrupts();
}

__attribute__((aligned(4096))) uint64_t ecc_test_bufc[32];
volatile static int bankd = 0;
void test_data_Cecc_error_all_banks(void)
{

    // 1. 选安全地址
    extern char _bss_end[];
    extern char _fw_end[];

    uint64_t *  addr = (uint64_t *) (((uintptr_t)_bss_end + 0x100) & ~0x3FUL); // align to 64B

    if (addr + 64 > (uint64_t *) _fw_end) {
//        sbi_printf("No safe space: bss_end=0x%ld, fw_end=0x%ld\n", bss_end, fw_end);
        return;
    }

   addr[0] = 0xa55bbaa789ABCD0ULL;

   bankd++;
   bankd = (bankd)%BANK_COUNT;

 //   uint64_t ecc_test_buf[32] __attribute__((aligned(64)));
    for (int i = 0; i < 32; i++) {
        ecc_test_bufc[i] = 0xDEADBEEFCAFEBABEULL;
    }

    //    sbi_printf("BSS end: 0x%p, addr=0x%p, &ecc_test_buf[%d]=0x%p\n", &_bss_end, addr, bankd*4, &ecc_test_buf[bankd*4]);
//    sbi_printf("=== Polling-based Data ECC Test ===\n");

    // Step 1: 清理状态
    disable_interrupts();

    init_clear_ecc_injection();
   // ecc_write(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET, 0);
    spin_delay_ms(10);

    ecc_write(BEU_LOCAL_INTR, 0xFF);  // 禁用所有 BEU 中断

    spin_delay_ms(20);

    asm volatile("fence iorw, iorw" ::: "memory");

 //   for (int bankc = 0; bankc < BANK_COUNT; bankc++) {
  //      sbi_printf("Testing Bank %d...\n", bank);

        // Step 2: 配置 ECCMASK
        ecc_write(CTRLUNIT_BASE_ADDR + ECCMASK_OFFSET + bankd * 8, eccmask_valuesC[bankd] /*+ handle_count + 5*/);
	asm volatile("fence w, w" ::: "memory");

        // Step 3: 配置 ECCCTL (one-hot mask, PST=0 for single shot)
        uint64_t ctl = (1ULL << 0) |   // ESE=1
                       (0ULL << 1) |   // PST=0 (single trigger)
                       (1ULL << 2) |   // EDE=1 (wait until ECCEID=0)
                       (1ULL << 3) |   // CMP=1 (data)
                       (1ULL << (4 + bankd)); //Failed. // Enable bank 'bank'

        ecc_write(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET, 16); // Short delay
        ecc_write(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET, ctl);
	asm volatile("fence w, w" ::: "memory");

//      sbi_printf("Bank %d, set_ctl=0x%lx,back_ctl=0x%lx. 1\n", bank, ctl, ecc_read(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET));

    //   }


   //  sbi_printf("Testing Bank %d.., readback_ecceid=0x%lx. 2\n", bank, ecc_read(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET));

    // Step 4: 强制 Cache Miss

 //     for (int loop =0; loop < 10; loop++) {
//	cbo_cache_flush(&ecc_test_bufc[bankd*4]);

        // Step 5: 触发访问
        volatile uint64_t dummy = ecc_test_bufc[bankd]; //addr[0];
 //       volatile uint64_t  xx = dummy+1; 
        (void)dummy;
	//sbi_timer_mdelay(1);
 //     }
      asm volatile("fence iorw, iorw" ::: "memory");

         // sbi_printf("Bank %d,back_eid=0x%lx.\n", bank, ecc_read(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET));
        // Step 7: 清理，准备下一轮
     //   sbi_timer_mdelay(10);
 //   }
	enable_interrupts();
    sbi_printf("=== Polling data C...  ecc, ecc_test_bufc[%d]=0x%p Test Completed ===\n", bankd, &ecc_test_bufc[bankd]);
}

// Random 64-bit ECCMASK values for 8 banks (newly generated)
const uint64_t eccmask_valuesB[8] = {
    0x3F7A9C2E1D5B8463ULL,  // Bank 0
    0xA1B2C3D4E5F60789ULL,  // Bank 1
    0x00AA55FF00BB66EEULL,  // Bank 2
    0xF1E2D3C4B5A69788ULL,  // Bank 3
    0x1234ABCD5678EF90ULL,  // Bank 4
    0xDEADBEEFCAFEB00BULL,  // Bank 5
    0x7F7F7F7F7F7F7F7FULL,  // Bank 6
    0xC0FFEE1234567890ULL   // Bank 7
};

__attribute__((aligned(64))) uint64_t ecc_test_bufb[32] ;
volatile static int bankb = 0;
void test_tag_ecc_polling(void)
{
    // 1. 选安全地址
    extern char _bss_end[];
    extern char _fw_end[];
  //  uintptr_t  bss_end = (uintptr_t )&_bss_end;
 //   uintptr_t  fw_end  = (uintptr_t )&_fw_end;
    uint64_t *  addr = (uint64_t *) (((uintptr_t)_bss_end + 0x100) & ~0x3FUL); // align to 64B
    

    if (addr + 64 > (uint64_t *)_fw_end) {
  //      sbi_printf("No safe space: bss_end=0x%ld, fw_end=0x%ld\n", bss_end, fw_end);
        return;
    }

    addr[0] = 0x123456789ABCDEF0ULL;
 //   bankb++;
//    bankb = (bankb)%BANK_COUNT;
 //   uint64_t ecc_test_buf[32] __attribute__((aligned(64)));
    for (int i = 0; i < 32; i++) {
        ecc_test_bufb[i] = 0xDEADBEEFCAFEBABEULL;
    }
   
//     sbi_printf("BSS end: 0x%p, addr=0x%p, &ecc_test_bufb[%d]=0x%p\n", &_bss_end, addr, bankb*4, &ecc_test_bufb[bankb*4]);
     disable_interrupts();
     init_clear_ecc_injection();
     //beu_clear_status();
  //  ecc_write(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET, 0);
    spin_delay_ms(10);

    // 2. 禁用中断
    ecc_write(BEU_LOCAL_INTR, 0xFF);

    spin_delay_ms(20);
    asm volatile("fence iorw, iorw" ::: "memory");

//     for (int j = 0; j < BANK_COUNT; j++) {
     //   sbi_printf("Testing Bank %d...\n", bank);

    ecc_write(CTRLUNIT_BASE_ADDR + ECCMASK_OFFSET , 0xFF /*+ handle_count + 3*/);	
    asm volatile("fence w, w" ::: "memory");

    // 3. 配置 Tag ECC
    uint64_t ctl = (1ULL << 0) | (0ULL << 1) | (1ULL << 2) | (0ULL << 3) | (1ULL << (4));
    ecc_write(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET, 50);
    ecc_write(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET, ctl);

     asm volatile("fence w, w" ::: "memory");

  //  sbi_printf("Testing Bank %d.., set_ctl=0x%lx, readback_eccctl=0x%lx. 1\n", bank, ctl, ecc_read(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET));

  //   cbo_cache_flush(&ecc_test_bufb[bankb]);
    // 5. 强制 miss + 访问
    // cbo_cache_inval(&ecc_test_buf[bankb*4]);  // addr 
    volatile uint64_t x =  ecc_test_bufb[bankb]; //addr[0];
           (void)x;
	   (void)x;
	   (void)x;
//    volatile uint64_t  xx = x+1;	   
	asm volatile("fence iorw, iorw" ::: "memory");
    // 7. 清理
  //  sbi_timer_mdelay(100);
  //  }
    enable_interrupts();

    bankb++;
    bankb = (bankb)%32;

      sbi_printf("=== Polling Test tag ecc  Completed ===\n");
}

void  Init_Tag_Ecc(void)
{
	disable_interrupts();
	init_clear_ecc_injection();

	spin_delay_ms(10);

	ecc_write(BEU_LOCAL_INTR, 0xFF);

	spin_delay_ms(20);
	asm volatile("fence iorw, iorw" ::: "memory");


	ecc_write(CTRLUNIT_BASE_ADDR + ECCMASK_OFFSET , 0xFF );
	asm volatile("fence w, w" ::: "memory");

	// 3. 配置 Tag ECC
	uint64_t ctl = (1ULL << 0) | (0ULL << 1) | (1ULL << 2) | (0ULL << 3) | (1ULL << (4));
	ecc_write(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET, 50);
	ecc_write(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET, ctl);

	asm volatile("fence w, w" ::: "memory");
	enable_interrupts();
}

void  loop_NMItest(void)
{
	return ;

  for(int k=0; k<1; k++)
  {
	test_data_Decc_error_all_banks();
	spin_delay_ms(1000);
	Get_print_event();

    //  	test_data_ecc_error_all_banks( );
  //	spin_delay_ms(1000);
    //    Get_print_event();

   //   	test_tag_ecc_polling( );
    //	spin_delay_ms(1000);
    //    Get_print_event();

      /*	test_data_Cecc_error_all_banks( );
        spin_delay_ms(1000);
	Get_print_event();

        test_data_Decc_error_all_banks( );
	Get_print_event(); */
//      sbi_timer_mdelay(1500);
  }

}

#define ECCCTL_ESE_BIT 0     // error signaling enable
#define ECCCTL_PST_BIT 1     // persistent injection, not use, we just trigger once ecc error
#define ECCCTL_EDE_BIT 2     // error delay enable
#define ECCCTL_CMP_BIT 3     // component (0: tag, 1: data)
#define ECCCTL_BANK_BIT 4    // bank enable, 8 bit, every bit enable one mask, 0b0000_0010 means enable mask1

__attribute__((aligned(4096))) uint64_t test_data_array[32] ;
void test_data_ecc_error(int index) {
    volatile uint64_t *target = &test_data_array[index];  // 直接使用数组

	*target = 0xDEADBEEFCAFEBABEULL +1;
	asm volatile("fence w, w" ::: "memory");

	disable_interrupts();
    for(int bank_num = index; bank_num < index + 1; bank_num++) {
      // 1. set ECCMASK
      uint64_t data_mask = 0x3f3f3f3f3f3f3f3fULL; //eccmask_valuesC[bank_num%BANK_COUNT]; //0x3f3f3f3f3f3f3f3f; // hit ecc error
      ecc_write(CTRLUNIT_BASE_ADDR + ECCMASK_OFFSET + (bank_num * 0x8), data_mask);

      // 2. set ECCEID
      ecc_write(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET, 10);

      asm volatile("fence w, w" ::: "memory");
      // 3. set ECCCTL
      uint64_t ctl_value = (1 << ECCCTL_ESE_BIT) | (0 << ECCCTL_PST_BIT) |
                           (1 << ECCCTL_EDE_BIT) | (1 << ECCCTL_CMP_BIT) |
                           ((1 << bank_num) << ECCCTL_BANK_BIT);
      ecc_write(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET, ctl_value);

       asm volatile("fence w, w" ::: "memory");
	   
	/* for (int i = 0; i < 3; i++) {
        volatile uint64_t dummy = *target;
        (void)dummy;
    } */
      // 4. 立即触发 - 完全展开 FOR_NUM 次内联汇编，避免循环和栈变量
      asm volatile(
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
         "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "ld t0, 0(%0)\n\t"
          "fence\n\t"
          : : "r"(target) : "t0", "memory");
      spin_delay_ms(2);
    }
      enable_interrupts();
    //   sbi_printf("=== test_data_ecc_error Completed ===\n");
}

void test_data_Decc_error_all_banks(void)
{
    int i;
    for(i=0; i<sizeof(test_data_array)/sizeof(test_data_array[0]); i++) {
		test_data_array[i] = 0xDEADBEEFCAFEBABEULL + i;
    }	
	asm volatile("fence w, w" ::: "memory");
	 
    for(i=0; i<8; i++) {
	   
	 test_data_ecc_error(i);  
    }
}

static inline bool is_compressed_insn(uintptr_t pc)
{
	uint16_t insn = *(volatile uint16_t *)pc;
	return (insn & 0x3) != 0x3;
}

//volatile uint64_t  save_pa[100];

void sbi_handle_nmi(void)
{
    // ❌ 禁止任何内存访问！包括 printf、readq、writeq
    // ✅ 仅使用 CSR 操作
//	 *(volatile uint64_t *)BEU_LOCAL_INTR = 0x00;
        uint64_t saved_beu_intr = ecc_read(BEU_LOCAL_INTR);
	 ecc_write(BEU_LOCAL_INTR, 0);

	asm volatile("fence w, w" ::: "memory");

        cause = ecc_read(BEU_CAUSE);
	pa    = ecc_read(BEU_VALUE);
//	save_pa[handle_count] = pa;
//	save_cause[handle_count] = cause;
	handle_count++ ;

//	readq((volatile u64 *)0x87001300);
        writeq(0x01, (volatile u64 *)0x87001300);
        //value =  readq((volatile u64 *)0x87001300);

	 //sbi_printf("✅ ECC Triggered! CAUSE=0x%lx, PA=0x%lx\n", cause, pa);	
	
//	 ecc_write(BEU_CAUSE, 0);
//	 ecc_write(BEU_VALUE, 0);

//	 ecc_write(BEU_LOCAL_INTR, saved_beu_intr);

	 return ;

	 mncause = csr_read(CSR_MNCAUSE);
	 mnepc   = csr_read(CSR_MNEPC);
//    uint64_t mepc = csr_read(CSR_MEPC); // 注意：NMI 是 MNEPC，不是 MEPC！
     // uint64_t saved_beu_intr = ecc_read(BEU_LOCAL_INTR);
         ecc_write(BEU_LOCAL_INTR, 0);

   //  sbi_printf("===  sbi_handle_nmi  ===\n");
   //     cause = ecc_read(BEU_CAUSE);
   //     pa    = ecc_read(BEU_VALUE);
	acc_intr = ecc_read(BEU_ACCRUED_INTR);
   

	if (is_compressed_insn(mnepc)) {
		mnepc += 2;   // RVC: 16-bit
	} else {
		mnepc += 4;   // Standard: 32-bit
	}
	csr_write(CSR_MNEPC, mnepc);

     beu_clear_status();
     ecc_write(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET, 0);
     ecc_write(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET, 0);
     ecc_write(BEU_LOCAL_INTR, saved_beu_intr);
    // 尝试跳过错误指令

    // 直接返回，不清除 BEU（避免访问 BEU 寄存器）
    // 系统可能继续运行（如果只是单次错误）
}


void init_clear_ecc_injection(void)
{
    // 清除所有 ECC 注入状态

	writeq(0, (volatile uint64_t *)BEU_LOCAL_INTR);

    for (int bankc = 0; bankc < BANK_COUNT; bankc++) {
        uint64_t ctl = (0ULL << 0) |   // ESE=1: enable injection
                       (0ULL << 1) |   // PST=1: pulse trigger (repeatable)
                       (0ULL << 2) |   // EDE=1: delay until ECCEID=0
                       (0ULL << 3) |   // CMP=0: inject to tag
                       (1ULL << (bankc + ECCCTL_BANK_BIT)); // Bank select (bit11:4) ((uint64_t)bank << 4);

     writeq(ctl, (volatile uint64_t *)(CTRLUNIT_BASE_ADDR + ECCCTL_OFFSET));
     writeq(0,(volatile uint64_t *)(CTRLUNIT_BASE_ADDR + ECCMASK_OFFSET + bankc*8));
    }

    writeq(0,  (volatile uint64_t *)(CTRLUNIT_BASE_ADDR + ECCEID_OFFSET));
 //   sbi_printf("SBI: before accurd-intr=0x%lx, cause=0x%lx...\n", readq((volatile uint64_t *)BEU_ACCRUED_INTR), readq( (volatile uint64_t *)BEU_CAUSE));

    // 清除 BEU 状态
    writeq(0,  (volatile uint64_t *)BEU_ACCRUED_INTR);
    writeq(0,  (volatile uint64_t *)BEU_CAUSE);

    writeq(0,  (volatile uint64_t *)BEU_VALUE);  // debug . 
    asm volatile("fence iorw, iorw" ::: "memory");

//    sbi_printf("SBI: after accurd-intr=0x%lx, cause=0x%lx...\n", readq((volatile uint64_t *)BEU_ACCRUED_INTR), readq( (volatile uint64_t *)BEU_CAUSE));
}

#define	   DATA_ECC	1
#define    TAG_ECC      2
#define    RESULT_ECC   3
static int sbi_xs_nmi_test_handler(unsigned long extid, unsigned long funcid,
                                  struct sbi_trap_regs *regs,
                                  struct sbi_ecall_return *out)
{
        switch (funcid) {
	case DATA_ECC:
		test_data_ecc_error_all_banks();
		sbi_printf(" data ecc inject test ...\n");
		break;
	case TAG_ECC:
		test_tag_ecc_polling();
		sbi_printf(" tag ecc inject test ...\n");
		break;
	case RESULT_ECC:
		Get_print_event();
		break;	
	default:
	  	break;
	}
	return 0 ;
}

struct sbi_ecall_extension ecall_xs_nmi_test;

static int sbi_ecall_nmi_register_extensions(void)
{
        sbi_printf("SBI: sbi_ecall_nmi_register_extensions zzk  ...\n");
        return sbi_ecall_register_extension(&ecall_xs_nmi_test);
}

struct sbi_ecall_extension ecall_xs_nmi_test = {
        .extid_start            = SBI_EXT_XS_NMI_TEST,
        .extid_end              = SBI_EXT_XS_NMI_TEST,
        .register_extensions    = sbi_ecall_nmi_register_extensions,
        .handle                 = sbi_xs_nmi_test_handler,
};
