// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Real vocoder wrapper and workspace ownership, using C functions instead of any codec image.
#include <stdio.h>
#include <string.h>
#include "audio_host.h"
#include "../../platform/hrc7000/codec_slot.h"
static bool fake_slot_check(const uint8_t *, uint32_t, const codec_windows_t *, uint32_t *);
#define codec_slot_check fake_slot_check
#define RFOUNDRY_BOARD_ID "test"
#include "../../platform/hrc7000/vocoder.c"
#undef codec_slot_check
#if CONFIG_P25
#include "p25/p25_workspace.h"
_Static_assert(sizeof(p25_workspace_t) <= sizeof enc_state, "P25 workspace exceeds the loan pool");
#endif

const uint8_t __codec_slot[128] = {0}, __codec_slot_end[1] = {0};
const uint8_t __codec_run_lo[1] = {0}, __codec_run_hi[1] = {0};
const uint8_t __codec_tab_lo[1] = {0}, __codec_tab_hi[1] = {0};

static int checks, failures, enc_inits, dec_inits, enc_calls, dec_calls, delays;
static bool masked, park_during_load, try_nested_loan;
static codec_slot_t fake_slot;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)

uint32_t audio_host_irq_save(void) { bool was = masked; masked = true; return was; }
void audio_host_irq_restore(uint32_t saved) { masked = saved != 0; }
bool audio_host_in_isr(void) { return false; }
void audio_host_barrier(void) {}
void vTaskDelay(TickType_t ticks) { CHECK(ticks == 1 && !masked); delays++; busy = false; }

static bool fake_slot_check(const uint8_t *image, uint32_t room, const codec_windows_t *windows, uint32_t *run)
{
	(void)image; (void)room; (void)windows; (void)run;
	CHECK(!masked);
	if (park_during_load)
		parked = true;
	return false; // exercise absent-slot behavior without loading or executing an image
}

static bool filled(const void *data, size_t bytes, uint8_t value)
{
	const uint8_t *p = data;
	for (size_t i = 0; i < bytes; i++)
		if (p[i] != value)
			return false;
	return true;
}

static int fake_enc_init(void *state, int a, int b)
{
	CHECK(!masked && busy && state == enc_state && a == 1 && b == 1);
	CHECK(((uint8_t *)state)[0] == 0 && *h16(state, 0x173C) == 128);
	enc_inits++;
	return 0;
}
static int fake_enc_set(void *state, int a, int b)
{
	CHECK(state == enc_state && a == 128 && b == -20);
	return 0;
}
static int fake_dec_init(void *state, int mode)
{
	CHECK(state == dec_state && mode == 1 && ((uint8_t *)state)[0] == 0);
	dec_inits++;
	return 0;
}
static int fake_dec_set(void *state, int a, int b)
{
	CHECK(state == dec_state && a == 128 && b == -20);
	return 0;
}
static int fake_fec_init(void *state, int mode) { (void)state; CHECK(mode == 1); return 0; }
static int fake_decode(int16_t *out, int n, int16_t *bits, int r3, int flags, int half, void *state)
{
	(void)bits; (void)r3; (void)flags; (void)half;
	CHECK(!masked && busy && state == dec_state && n == HALF);
	for (int i = 0; i < n; i++) out[i] = 2200;
	dec_calls++;
	return 0;
}
static int fake_encode(int16_t *bits, int r1, const int16_t *in, int n, int flags, int half, int s8, void *state)
{
	(void)r1; (void)in; (void)flags; (void)half; (void)s8;
	CHECK(!masked && busy && state == enc_state && n == HALF);
#if CONFIG_P25
	if (try_nested_loan) {
		hal_vocoder_loan_t nested = {0};
		CHECK(!hrc_vocoder.lend(16, 4, &nested) && nested.data == NULL);
	}
#else
	(void)try_nested_loan;
#endif
	for (int i = 0; i < 72; i++) bits[i] = 1;
	enc_calls++;
	return 0;
}

static void install_fake_codec(void)
{
	uintptr_t entries[] = { (uintptr_t)fake_decode, (uintptr_t)fake_encode, (uintptr_t)fake_dec_init,
		(uintptr_t)fake_dec_set, (uintptr_t)fake_enc_init, (uintptr_t)fake_enc_set, (uintptr_t)fake_fec_init };
	code_ram = entries[0];
	for (size_t i = 1; i < sizeof entries / sizeof entries[0]; i++)
		if (entries[i] < code_ram) code_ram = entries[i];
	fake_slot.decode = (uint32_t)(entries[0] - code_ram);
	fake_slot.encode = (uint32_t)(entries[1] - code_ram);
	fake_slot.dec_init = (uint32_t)(entries[2] - code_ram);
	fake_slot.dec_set = (uint32_t)(entries[3] - code_ram);
	fake_slot.enc_init = (uint32_t)(entries[4] - code_ram);
	fake_slot.enc_set = (uint32_t)(entries[5] - code_ram);
	fake_slot.fec_init = (uint32_t)(entries[6] - code_ram);
	fake_slot.stock_tag = 49;
	slot = &fake_slot;
	presence = 1;
	ready = parked = busy = false;
}

#if CONFIG_P25
static uint8_t bad_pool[8];
static int bad_returns;
static bool refuse_return;
static int bad_kind;
static bool bad_lend(size_t bytes, size_t alignment, hal_vocoder_loan_t *out)
{
	(void)bytes; (void)alignment;
	*out = (hal_vocoder_loan_t){ .data = bad_pool, .bytes = sizeof bad_pool, .token = 7 };
	if (bad_kind == 1) out->data = NULL;
	if (bad_kind == 2) { out->bytes = sizeof(p25_workspace_t); out->token = 0; }
	if (bad_kind == 3) {
		out->data = (uint8_t *)enc_state + 1;
		out->bytes = sizeof(p25_workspace_t);
	}
	return true;
}
static bool bad_reclaim(hal_vocoder_loan_t *loan)
{
	bad_returns++;
	if (refuse_return) return false;
	*loan = (hal_vocoder_loan_t){0};
	return true;
}
#endif

int main(void)
{
	install_fake_codec();
	int16_t pcm[VOCODER_FRAME_SAMPLES] = {0};
	uint8_t bits[VOCODER_BITS] = {0};
	CHECK(hrc_vocoder.present());
	try_nested_loan = true;
	hrc_vocoder.encode(pcm, bits);
	CHECK(enc_inits == 1 && dec_inits == 1 && enc_calls == 2 && filled(bits, sizeof bits, 1));
	CHECK(ready && !busy && !masked);
#if CONFIG_P25
	CHECK(hrc_vocoder.lend && hrc_vocoder.reclaim);
	hal_vocoder_loan_t loan = {0}, other = {0};
	CHECK(!hrc_vocoder.lend(0, 4, &loan));
	CHECK(!hrc_vocoder.lend(sizeof enc_state + 1, 4, &loan));
	CHECK(!hrc_vocoder.lend(1, 0, &loan));
	CHECK(!hrc_vocoder.lend(1, 3, &loan));
	CHECK(!hrc_vocoder.lend(1, 16, &loan));
	CHECK(!hrc_vocoder.lend(1, 4, NULL));
	busy = true;
	CHECK(!hrc_vocoder.lend(1, 4, &loan) && ready);
	busy = false;
	hrc_vocoder.set_options(VOCODER_OPT_AGC);
	uint32_t flags_before = hrc_vocoder.encode_flags();
	CHECK(hrc_vocoder.lend(sizeof enc_state, 8, &loan));
	CHECK(loan.data == enc_state && loan.bytes == sizeof enc_state && loan.token != 0 && !ready);
	CHECK((uintptr_t)loan.data % 8 == 0 && !hrc_vocoder.present());
	CHECK(!strcmp(hal_vocoder_id(&hrc_vocoder), "test-049") && hrc_vocoder_tag() == 49);
	CHECK(!hrc_vocoder.lend(1, 4, &other));
	CHECK(!hrc_vocoder.lend(1, 4, &loan));
	memset(loan.data, 0xA5, loan.bytes);
	int before = enc_inits + dec_inits + enc_calls + dec_calls;
	hrc_vocoder.reset();
	hrc_vocoder.encode(pcm, bits);
	CHECK(filled(bits, sizeof bits, 0));
	memset(pcm, 0x55, sizeof pcm);
	hrc_vocoder.decode(bits, pcm);
	CHECK(filled(pcm, sizeof pcm, 0));
	CHECK(before == enc_inits + dec_inits + enc_calls + dec_calls);
	CHECK(filled(loan.data, loan.bytes, 0xA5) && !busy);
	hal_vocoder_loan_t stale = loan, malformed = loan;
	malformed.token++;
	CHECK(!hrc_vocoder.reclaim(&malformed));
	malformed = loan; malformed.data = bad_pool;
	CHECK(!hrc_vocoder.reclaim(&malformed));
	malformed = loan; malformed.bytes--;
	CHECK(!hrc_vocoder.reclaim(&malformed));
	CHECK(!hrc_vocoder.reclaim(NULL));
	CHECK(hrc_vocoder.reclaim(&loan) && loan.data == NULL && loan.bytes == 0 && loan.token == 0);
	CHECK(!hrc_vocoder.reclaim(&loan));
	CHECK(hrc_vocoder.present() && !ready && hrc_vocoder.encode_flags() == flags_before);
	int init_before = enc_inits;
	hrc_vocoder.decode(bits, pcm);
	CHECK(enc_inits == init_before + 1 && ready && pcm[0] == 2200 && pcm[159] == 2200);
	CHECK(hrc_vocoder.lend(1, 4, &loan));
	CHECK(loan.token != stale.token && !hrc_vocoder.reclaim(&stale) && !hrc_vocoder.present());
	CHECK(hrc_vocoder.reclaim(&loan));

	p25_workspace_lease_t lease = {0};
	const hal_vocoder_t absent = {0}, bad = { .lend = bad_lend, .reclaim = bad_reclaim };
	CHECK(!p25_workspace_acquire(NULL, &hrc_vocoder));
	CHECK(!p25_workspace_acquire(&lease, NULL));
	CHECK(!p25_workspace_acquire(&lease, &absent));
	memset(enc_state, 0x55, sizeof enc_state);
	CHECK(p25_workspace_acquire(&lease, &hrc_vocoder));
	CHECK(lease.vocoder == &hrc_vocoder && !hrc_vocoder.present());
	CHECK(filled(lease.loan.data, sizeof(p25_workspace_t), 0));
	CHECK(filled((uint8_t *)lease.loan.data + sizeof(p25_workspace_t), sizeof enc_state - sizeof(p25_workspace_t), 0x55));
	CHECK(!p25_workspace_acquire(&lease, &hrc_vocoder));
	((p25_workspace_t *)lease.loan.data)->engine.tx_stop = true;
	p25_workspace_lease_t stale_lease = lease;
	CHECK(p25_workspace_release(&lease) && lease.vocoder == NULL && lease.loan.data == NULL);
	CHECK(!p25_workspace_release(&lease) && !p25_workspace_release(NULL));
	CHECK(p25_workspace_acquire(&lease, &hrc_vocoder));
	CHECK(!p25_workspace_release(&stale_lease) && !hrc_vocoder.present());
	CHECK(p25_workspace_release(&lease));
	CHECK(!p25_workspace_acquire(&lease, &bad) && bad_returns == 1 && lease.vocoder == NULL);
	for (bad_kind = 1; bad_kind <= 3; bad_kind++)
		CHECK(!p25_workspace_acquire(&lease, &bad) && lease.vocoder == NULL);
	bad_kind = 0;
	refuse_return = true;
	CHECK(!p25_workspace_acquire(&lease, &bad) && lease.vocoder == &bad);
	CHECK(!p25_workspace_release(&lease));
	refuse_return = false;
	CHECK(p25_workspace_release(&lease));

	presence = -1;
	CHECK(hrc_vocoder.lend(1, 4, &loan));
	CHECK(presence == 0 && hrc_vocoder.profile() == NULL);
	CHECK(hrc_vocoder.reclaim(&loan) && !hrc_vocoder.present());
	presence = -1;
	park_during_load = true;
	CHECK(!hrc_vocoder.lend(1, 4, &loan) && loan.data == NULL && loan_token == 0 && !busy);
	park_during_load = false;
	parked = false;
	CHECK(hrc_vocoder.lend(1, 4, &loan));
	hrc_vocoder_park();
	CHECK(hrc_vocoder.reclaim(&loan) && !hrc_vocoder.present());
	CHECK(!hrc_vocoder.lend(1, 4, &loan));
#else
	CHECK(hrc_vocoder.lend == NULL && hrc_vocoder.reclaim == NULL);
#endif
	parked = false;
	busy = true;
	hrc_vocoder_park();
	CHECK(parked && !busy && delays == 1 && !masked);
	printf("vocoderMemoryTest: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
