/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Authors:
 *   yangyinglu<yangyinglu@bosc.com>
 *
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/riscv_elf.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_trap.h>

#define BEU_LOCAL_INTR    0x38010028UUL

.global cbo_cache_inval
cbo_cache_inval:
        /*
         * rs1 = a0 (base)
         * cbo.inval base
         * 000000000000 01010 010 00000 0001111
         */
        .word 0x0005200F
        ret

.global cbo_cache_flush
cbo_cache_flush:
        /*
         * rs1 = a0 (base)
         * cbo.flush base
         * 000000000010 01010 010 00000 0001111
         */
        .word 0x25200f
        ret

.macro	TRAP_SAVE_AND_SETUP_SP_T0_NMI
	/* Swap TP and MNSCRATCH */
	csrrw	tp, CSR_MSCRATCH, tp

	/* Save T0 in scratch space */
	REG_S	t0, SBI_SCRATCH_TMP0_OFFSET(tp)

	/*
	 * Set T0 to appropriate exception stack
	 *
	 * Came_From_M_Mode = ((MSTATUS.MPP < PRV_M) ? 1 : 0) - 1;
	 * Exception_Stack = TP ^ (Came_From_M_Mode & (SP ^ TP))
	 *
	 * Came_From_M_Mode = 0    ==>    Exception_Stack = TP
	 * Came_From_M_Mode = -1   ==>    Exception_Stack = SP
	 */
	csrr	t0, CSR_MNSTATUS
	srl	t0, t0, MNSTATUS_MPP_SHIFT
	and	t0, t0, PRV_M
	slti	t0, t0, PRV_M
	add	t0, t0, -1
	xor	sp, sp, tp
	and	t0, t0, sp
	xor	sp, sp, tp
	xor	t0, tp, t0

	/* Save original SP on exception stack */
	REG_S	sp, (SBI_TRAP_REGS_OFFSET(sp) - SBI_TRAP_CONTEXT_SIZE)(t0)

	/* Set SP to exception stack and make room for trap context */
	add	sp, t0, -(SBI_TRAP_CONTEXT_SIZE)

	/* Restore T0 from scratch space */
	REG_L	t0, SBI_SCRATCH_TMP0_OFFSET(tp)

	/* Save T0 on stack */
	REG_S	t0, SBI_TRAP_REGS_OFFSET(t0)(sp)

	/* Swap TP and MNSCRATCH */
	csrrw	tp, CSR_MSCRATCH, tp
.endm

.macro	TRAP_SAVE_MNEPC_MNSTATUS
	/* Save MNEPC and MNSTATUS CSRs */
	csrr	t0, CSR_MNEPC
	REG_S	t0, SBI_TRAP_REGS_OFFSET(mnepc)(sp)
	csrr	t0, CSR_MNSTATUS
	REG_S	t0, SBI_TRAP_REGS_OFFSET(mnstatus)(sp)
.endm

.macro	TRAP_RESTORE_MNEPC_MNSTATUS
	/* Restore MEPC and MSTATUS CSRs */
	REG_L	t0, SBI_TRAP_REGS_OFFSET(mnepc)(a0)
	csrw	CSR_MNEPC, t0
	REG_L	t0, SBI_TRAP_REGS_OFFSET(mnstatus)(a0)
	csrw	CSR_MNSTATUS, t0
.endm

.macro	TRAP_SAVE_NMI_INFO
	csrr	t0, CSR_MNCAUSE
	REG_S	t0, (SBI_TRAP_REGS_SIZE + SBI_TRAP_INFO_OFFSET(mncause))(sp)
.endm

.macro	TRAP_CALL_nmi_handler_ROUTINE
	/* Call C routine */
	add	a0, sp, zero
	call	sbi_handle_nmi /*xiangshan_nmi_handler */
.endm

.macro	TRAP_SAVE_GENERAL_REGS_EXCEPT_SP_T0_NMI
	/* Save all general regisers except SP and T0 */
	REG_S	zero, SBI_TRAP_REGS_OFFSET(zero)(sp)
	REG_S	ra, SBI_TRAP_REGS_OFFSET(ra)(sp)
	REG_S	gp, SBI_TRAP_REGS_OFFSET(gp)(sp)
	REG_S	tp, SBI_TRAP_REGS_OFFSET(tp)(sp)
	REG_S	t1, SBI_TRAP_REGS_OFFSET(t1)(sp)
	REG_S	t2, SBI_TRAP_REGS_OFFSET(t2)(sp)
	REG_S	s0, SBI_TRAP_REGS_OFFSET(s0)(sp)
	REG_S	s1, SBI_TRAP_REGS_OFFSET(s1)(sp)
	REG_S	a0, SBI_TRAP_REGS_OFFSET(a0)(sp)
	REG_S	a1, SBI_TRAP_REGS_OFFSET(a1)(sp)
	REG_S	a2, SBI_TRAP_REGS_OFFSET(a2)(sp)
	REG_S	a3, SBI_TRAP_REGS_OFFSET(a3)(sp)
	REG_S	a4, SBI_TRAP_REGS_OFFSET(a4)(sp)
	REG_S	a5, SBI_TRAP_REGS_OFFSET(a5)(sp)
	REG_S	a6, SBI_TRAP_REGS_OFFSET(a6)(sp)
	REG_S	a7, SBI_TRAP_REGS_OFFSET(a7)(sp)
	REG_S	s2, SBI_TRAP_REGS_OFFSET(s2)(sp)
	REG_S	s3, SBI_TRAP_REGS_OFFSET(s3)(sp)
	REG_S	s4, SBI_TRAP_REGS_OFFSET(s4)(sp)
	REG_S	s5, SBI_TRAP_REGS_OFFSET(s5)(sp)
	REG_S	s6, SBI_TRAP_REGS_OFFSET(s6)(sp)
	REG_S	s7, SBI_TRAP_REGS_OFFSET(s7)(sp)
	REG_S	s8, SBI_TRAP_REGS_OFFSET(s8)(sp)
	REG_S	s9, SBI_TRAP_REGS_OFFSET(s9)(sp)
	REG_S	s10, SBI_TRAP_REGS_OFFSET(s10)(sp)
	REG_S	s11, SBI_TRAP_REGS_OFFSET(s11)(sp)
	REG_S	t3, SBI_TRAP_REGS_OFFSET(t3)(sp)
	REG_S	t4, SBI_TRAP_REGS_OFFSET(t4)(sp)
	REG_S	t5, SBI_TRAP_REGS_OFFSET(t5)(sp)
	REG_S	t6, SBI_TRAP_REGS_OFFSET(t6)(sp)
.endm

.macro	TRAP_RESTORE_GENERAL_REGS_EXCEPT_A0_T0_NMI
	/* Restore all general regisers except A0 and T0 */
	REG_L	ra, SBI_TRAP_REGS_OFFSET(ra)(a0)
	REG_L	sp, SBI_TRAP_REGS_OFFSET(sp)(a0)
	REG_L	gp, SBI_TRAP_REGS_OFFSET(gp)(a0)
	REG_L	tp, SBI_TRAP_REGS_OFFSET(tp)(a0)
	REG_L	t1, SBI_TRAP_REGS_OFFSET(t1)(a0)
	REG_L	t2, SBI_TRAP_REGS_OFFSET(t2)(a0)
	REG_L	s0, SBI_TRAP_REGS_OFFSET(s0)(a0)
	REG_L	s1, SBI_TRAP_REGS_OFFSET(s1)(a0)
	REG_L	a1, SBI_TRAP_REGS_OFFSET(a1)(a0)
	REG_L	a2, SBI_TRAP_REGS_OFFSET(a2)(a0)
	REG_L	a3, SBI_TRAP_REGS_OFFSET(a3)(a0)
	REG_L	a4, SBI_TRAP_REGS_OFFSET(a4)(a0)
	REG_L	a5, SBI_TRAP_REGS_OFFSET(a5)(a0)
	REG_L	a6, SBI_TRAP_REGS_OFFSET(a6)(a0)
	REG_L	a7, SBI_TRAP_REGS_OFFSET(a7)(a0)
	REG_L	s2, SBI_TRAP_REGS_OFFSET(s2)(a0)
	REG_L	s3, SBI_TRAP_REGS_OFFSET(s3)(a0)
	REG_L	s4, SBI_TRAP_REGS_OFFSET(s4)(a0)
	REG_L	s5, SBI_TRAP_REGS_OFFSET(s5)(a0)
	REG_L	s6, SBI_TRAP_REGS_OFFSET(s6)(a0)
	REG_L	s7, SBI_TRAP_REGS_OFFSET(s7)(a0)
	REG_L	s8, SBI_TRAP_REGS_OFFSET(s8)(a0)
	REG_L	s9, SBI_TRAP_REGS_OFFSET(s9)(a0)
	REG_L	s10, SBI_TRAP_REGS_OFFSET(s10)(a0)
	REG_L	s11, SBI_TRAP_REGS_OFFSET(s11)(a0)
	REG_L	t3, SBI_TRAP_REGS_OFFSET(t3)(a0)
	REG_L	t4, SBI_TRAP_REGS_OFFSET(t4)(a0)
	REG_L	t5, SBI_TRAP_REGS_OFFSET(t5)(a0)
	REG_L	t6, SBI_TRAP_REGS_OFFSET(t6)(a0)
.endm
.macro TRAP_RESTORE_A0_T0_NMI
	/* Restore T0 */
	REG_L	t0, SBI_TRAP_REGS_OFFSET(t0)(a0)

	/* Restore A0 */
	REG_L	a0, SBI_TRAP_REGS_OFFSET(a0)(a0)
.endm


	.section .entry, "ax", %progbits
	.align 3
	.globl _kmh_v2_nmi_handler
_kmh_v2_nmi_handler:
	/* Save T0 in mnscratch space */

	csrrw	t0, CSR_MNSCRATCH, t0

	//csrr    t0, CSR_MCAUSE
	//csrr    t0, CSR_MEPC
	//csrr    t0, CSR_MCAUSE
	//csrr    t0, CSR_MNEPC
	
	//t0 eq 8 or 0
	csrr    t0, CSR_MNSTATUS
    andi    t0, t0, 0x8
    beqz    t0, 1f

	/* Restore T0 */
	csrrw	t0, CSR_MNSCRATCH, t0
    # bit3 == 0 → read MCAUSE
	j _trap_handler_hyp

1:    # bit3 == 1 → read MNCAUSE
	/* Restore T0 */
	csrrw	t0, CSR_MNSCRATCH, t0

	TRAP_SAVE_AND_SETUP_SP_T0_NMI

	TRAP_SAVE_MNEPC_MNSTATUS

TRAP_SAVE_GENERAL_REGS_EXCEPT_SP_T0_NMI

	TRAP_SAVE_NMI_INFO

	TRAP_CALL_nmi_handler_ROUTINE

TRAP_RESTORE_GENERAL_REGS_EXCEPT_A0_T0_NMI

	TRAP_RESTORE_MNEPC_MNSTATUS

	TRAP_RESTORE_A0_T0_NMI
#mret
    .word 0x70200073
