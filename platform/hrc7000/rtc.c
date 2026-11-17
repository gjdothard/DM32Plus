// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#include "config.h"
#if CONFIG_RTC
#include "platform.h"
#include "hal_rtc.h"
#include "hal_time.h"

// Internal I2C2, hardware RTC spec section 3.3.
#define I2C(off) REG32(0x14080000u + (off))
#define I2C_CON I2C(0x00)
#define I2C_TAR I2C(0x04)
#define I2C_DATA I2C(0x10)
#define I2C_SCL_HIGH I2C(0x1c)
#define I2C_SCL_LOW I2C(0x20)
#define I2C_INTR_MASK I2C(0x30)
#define I2C_RX_TL I2C(0x38)
#define I2C_TX_TL I2C(0x3c)
#define I2C_CLR_INTR I2C(0x40)
#define I2C_ENABLE I2C(0x6c)
#define I2C_STATUS I2C(0x70)
#define I2C_ENABLED I2C(0x9c)
#define I2C_START I2C(0xa0)
#define I2C_RX_READY 0x08u
#define I2C_IDLE_MASK 0x05u
#define I2C_TX_EMPTY 0x04u
#define I2C_TIMEOUT_US 1000u

#define RTC_SECOND 0x00u
#define RTC_LOAD_SECOND 0x0cu
#define RTC_CONTROL 0x11u
#define RTC_VALID 0x12u
#define RTC_RUN 0x02u
#define RTC_LOAD 0x04u

// Reserve the upper half of the day counter for our UTC epoch.
#define RTC_DAY_BASE 0x8000u

static bool wait_for(volatile uint32_t *reg, uint32_t mask, uint32_t value)
{
	uint32_t start = hal_micros();
	do {
		if ((*reg & mask) == value)
			return true;
	} while (hal_micros() - start < I2C_TIMEOUT_US);
	return false;
}

static bool transfer(uint8_t reg, uint8_t *value, bool read)
{
	I2C_START = 0;
	I2C_ENABLE = 0;
	if (!wait_for(&I2C_ENABLED, 1u, 0))
		return false;

	I2C_CON = 0x65;
	I2C_SCL_HIGH = 0x61;
	I2C_SCL_LOW = 0x68;
	I2C_RX_TL = I2C_TX_TL = 6;
	I2C_INTR_MASK = 0;
	(void)I2C_CLR_INTR;
	I2C_TAR = 0x70;
	I2C_ENABLE = 1;
	if (!wait_for(&I2C_ENABLED, 1u, 1u)) {
		I2C_ENABLE = 0;
		return false;
	}
	I2C_DATA = reg;
	I2C_DATA = read ? 0x100u : *value;
	I2C_START = 1;
	bool ok = read ? wait_for(&I2C_STATUS, I2C_RX_READY, I2C_RX_READY)
		       : wait_for(&I2C_STATUS, I2C_IDLE_MASK, I2C_TX_EMPTY);
	if (read && ok) {
		*value = (uint8_t)I2C_DATA;
		ok = wait_for(&I2C_STATUS, I2C_IDLE_MASK, I2C_TX_EMPTY);
	}
	I2C_START = 0;
	I2C_ENABLE = 0;
	return wait_for(&I2C_ENABLED, 1u, 0) && ok;
}

static bool write_reg(uint8_t reg, uint8_t value)
{
	return transfer(reg, &value, false);
}

static bool rtc_read(uint32_t *utc)
{
	uint8_t valid, control;
	if (!transfer(RTC_VALID, &valid, true) || !(valid & 1u) ||
	    !transfer(RTC_CONTROL, &control, true) || !(control & RTC_RUN))
		return false;

	uint8_t time[5], second;
	for (unsigned attempt = 0; attempt < 2; attempt++) {
		for (unsigned i = 0; i < sizeof(time); i++) {
			if (!transfer(RTC_SECOND + i, &time[i], true))
				return false;
		}
		// A second change could have carried into any of the other fields.
		if (!transfer(RTC_SECOND, &second, true))
			return false;
		if (second != time[0])
			continue;
		uint32_t day = time[3] | ((uint32_t)time[4] << 8);
		if (day < RTC_DAY_BASE || day >= RTC_DAY_BASE + RTC_DAYS ||
		    time[2] >= 24u || time[1] >= 60u || second >= 60u)
			return false;
		*utc = (day - RTC_DAY_BASE) * RTC_DAY_SECONDS + time[2] * 3600u + time[1] * 60u + second;
		return true;
	}
	return false;
}

static bool rtc_write(uint32_t utc)
{
	uint32_t day = utc / RTC_DAY_SECONDS + RTC_DAY_BASE;
	if (day >= RTC_DAY_BASE + RTC_DAYS || !write_reg(RTC_VALID, 0))
		return false;
	const uint8_t time[] = {
		utc % 60u, utc / 60u % 60u, utc / 3600u % 24u, day & 0xffu, day >> 8,
	};
	for (unsigned i = 0; i < sizeof(time); i++) {
		if (!write_reg(RTC_LOAD_SECOND + i, time[i]))
			return false;
	}
	// Leave alarms disabled. Only mark time valid after the load completes.
	if (!write_reg(RTC_CONTROL, RTC_RUN | RTC_LOAD))
		return false;
	for (unsigned attempt = 0; attempt < 3; attempt++) {
		uint8_t control;
		if (!transfer(RTC_CONTROL, &control, true))
			return false;
		if (!(control & RTC_LOAD))
			return (control & RTC_RUN) && write_reg(RTC_VALID, 1);
	}
	return false;
}

const hal_rtc_t hrc_rtc = { .read = rtc_read, .write = rtc_write };
#endif
