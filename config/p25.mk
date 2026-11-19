# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
# Kept separate so the real firmware source/notice selection can be host tested
ifeq ($(CONFIG_AIR),y)
CORE_DIRS += core/air core/fec
endif
ifeq ($(CONFIG_P25),y)
CORE_DIRS += core/p25
endif
ifeq ($(CONFIG_MBE),y)
CORE_DIRS += core/mbe
endif
ifeq ($(CONFIG_IMBE),y)
ABOUT_MORE += core/mbe/THIRD_PARTY
endif
