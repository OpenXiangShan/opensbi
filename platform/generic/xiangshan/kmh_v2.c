/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2025 Yangyinglu  <yangyinglu@bosc.cn>
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
#include <sbi/sbi_console.h>


static int kmh_v2_extensions_init(const struct fdt_match *match,
				     struct sbi_hart_features *hfeatures)
{
	return 0;
}

static int kmh_v2_final_init(bool cold_boot,
		const struct fdt_match *match)
{
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
	.final_init = kmh_v2_final_init,
	.extensions_init = kmh_v2_extensions_init,
	.pmu_init = kmh_v2_pmu_init,
};
