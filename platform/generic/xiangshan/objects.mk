#
# SPDX-License-Identifier: BSD-2-Clause
#

carray-platform_override_modules-$(CONFIG_PLATFORM_XIANGSHAN_KMH_V2) += kmh_v2

platform-objs-$(CONFIG_XIANGSHAN_KMHV2_ERRNMI) += xiangshan/kmh_v2_nmi_handler.o   xiangshan/sbi_ecall_xs_nmi_test.o
platform-objs-$(CONFIG_PLATFORM_XIANGSHAN_KMH_V2) += xiangshan/kmh_v2.o xiangshan/parse_dts.o

ifndef FW_CONFIG_TEXT_ADDR
FW_CONFIG_TEXT_ADDR = 0x90000000
endif

platform-genflags-$(CONFIG_PLATFORM_XIANGSHAN_KMH_V2) += -DFW_CONFIG_TEXT_ADDR=$(FW_CONFIG_TEXT_ADDR)
