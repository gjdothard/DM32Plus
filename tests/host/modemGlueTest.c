// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Execute the real platform glue/driver handoff with fake IRQ and RTOS services.
// Rename only the CPU instructions/MMIO timer acknowledge; keep platform IRQ IDs.
#include <stdio.h>
#include <string.h>
#define hrc_irq_save target_irq_save
#define hrc_irq_restore target_irq_restore
#define hrc_in_isr target_in_isr
#define hrc_barrier target_barrier
#define hrc_timer_ack target_timer_ack
#include "../../platform/hrc7000/platform.h"
#undef hrc_irq_save
#undef hrc_irq_restore
#undef hrc_in_isr
#undef hrc_barrier
#undef hrc_timer_ack

static int checks, failures, scheduler_depth, notices, gate_calls, releases, pa_calls;
static int callbacks, resumes;
static bool cpu_masked, in_irq, gate_allowed, poll_on_resume;
static uint32_t clock_us, modem_mask;
static bool irq_enabled[64];
static hrc_irq_fn handlers[64];
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)
static uint32_t hrc_irq_save(void) { bool prior = cpu_masked; cpu_masked = true; return prior; }
static void hrc_irq_restore(uint32_t state) { cpu_masked = state != 0; }
static bool hrc_in_isr(void) { return in_irq; }
static void hrc_barrier(void) {}
static void hrc_timer_ack(uint8_t n) { (void)n; }

#include "../../platform/hrc7000/dmr_glue.c"
#ifdef MODEM_TEST_PCM
#include "audio_pcm.h"
uint32_t audio_host_irq_save(void) { return hrc_irq_save(); }
void audio_host_irq_restore(uint32_t saved) { hrc_irq_restore(saved); }
bool audio_host_in_isr(void) { return hrc_in_isr(); }
void audio_host_barrier(void) { hrc_barrier(); }
void audio_host_timer_ack(uint8_t timer) { hrc_timer_ack(timer); }
#endif

uint32_t hrc_micros(void) { return clock_us; }
void hrc_irq_register(uint8_t n, hrc_irq_fn fn) { handlers[n] = fn; irq_enabled[n] = false; }
void hrc_irq_enable(uint8_t n, bool on) { irq_enabled[n] = on; }
uint32_t hrc_modem_irqs_mask(void) { uint32_t prior = modem_mask; modem_mask = 15; return prior; }
void hrc_modem_irqs_restore(uint32_t saved) { modem_mask = saved; }
void hrc_timer_stop(uint8_t n) { (void)n; }
void hrc_timer_oneshot(uint8_t n, uint32_t us) { (void)n; (void)us; }
void hrc_timer_periodic(uint8_t n, uint32_t us) { (void)n; (void)us; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *ctx,
		      uint32_t priority, TaskHandle_t *handle)
{
	(void)fn; (void)name; (void)stack; (void)ctx; (void)priority;
	*handle = (void *)1;
	return pdTRUE;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks) { (void)clear; (void)ticks; return 0; }
void vTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t *woken) { (void)t; *woken = pdFALSE; notices++; }
void xTaskNotifyGive(TaskHandle_t t) { (void)t; notices++; }
void vTaskSuspendAll(void) { scheduler_depth++; }
BaseType_t xTaskResumeAll(void)
{
	CHECK(scheduler_depth > 0);
	scheduler_depth--;
	resumes++;
	if (poll_on_resume && scheduler_depth == 0)
		modem_poll_once();
	return pdFALSE;
}

static bool rf_tx;
static void rf_switch(bool on) { rf_tx = on; }
static void pa_on(void) { pa_calls++; }
static bool gate(void)
{
	gate_calls++;
#ifdef MODEM_TEST_PCM
	return gate_allowed && hrc_audio_pcm_air_gate();
#else
	return gate_allowed;
#endif
}
static void release_gate(void)
{
	CHECK(!rf_tx);
#ifdef MODEM_TEST_PCM
	hrc_audio_pcm_air_release();
#endif
	releases++;
}
static void fm_half(void) {}
static void dmr_event(const dmr_event_t *ev, void *ctx) { (void)ev; (void)ctx; callbacks++; }
#if CONFIG_AIR
static void air_event_test(const air_event_t *ev, void *ctx)
{
	CHECK(!in_irq && scheduler_depth == 0);
	CHECK(ctx == &callbacks);
	if (ev->kind == AIR_EV_TX_DONE)
		callbacks++;
}
#endif
static const hrc_dmr_board_t legacy = { .rf_switch = rf_switch, .pa_on = pa_on, .pa_delay_us = 28000 };
static const hrc_dmr_board_t isolated = {
	.rf_switch = rf_switch, .pa_on = pa_on, .pa_delay_us = 28000,
	.air_tx_gate = gate, .air_tx_release = release_gate
};
#if CONFIG_HOTSPOT_SPLIT
// This suite checks ownership, not BS payload coding (covered by hrc7000BsTest)
bool bsOutboundRawSlot(const dmr_tx_burst_t *b, const dmr_cach_t *cach, int8_t ts, uint8_t cc,
		       const uint8_t *emb_lc, uint8_t *out)
{
	(void)b; (void)cach; (void)ts; (void)cc; (void)emb_lc;
	memset(out, 0x77, BS_RAW_SLOT_BYTES);
	return true;
}
#endif
static uint32_t regs[0x600 / 4];
static uint8_t buffer[0xA00];
static hrc7000_t dev;

int main(void)
{
	hrc7000_attach(&dev, regs, buffer);
#ifdef MODEM_TEST_PCM
	hrc_audio_pcm_attach(&dev, NULL);
	hrc_audio_pcm_ring(&dev, true);
#endif
	hrc_dmr_board_init(&dev, &legacy);
	CHECK(task != NULL && !active);
#if CONFIG_AIR
	air_profile_t profile = { .sym_rate = 4800, .levels = 4, .shape = AIR_SHAPE_C4FM, .dev_outer_hz = 1800 };
	CHECK(!hrc_air.open(&profile, air_event_test, &callbacks));
	CHECK(scheduler_depth == 0 && modem_mask == 0);
#endif
	hrc_dmr_enter();
	CHECK(active && eng.running && irq_enabled[IRQ_MODEM_SYS]);
	dmr_config_t cfg = { .cc = 1, .own_id = 123 };
	CHECK(hrc_dmr.start(&cfg, dmr_event, NULL));
#if CONFIG_AIR
	CHECK((hrc_air.fit(&profile) & AIR_FIT_RX) != 0);
	CHECK((hrc_air.fit(&profile) & AIR_FIT_TX) == 0);
	CHECK(!hrc_air.open(&profile, air_event_test, &callbacks)); // DMR stack still owns its queues
	hrc_dmr.stop();
	CHECK(hrc_dmr_fm_rx_hook(fm_half));
	CHECK(!hrc_air.open(&profile, air_event_test, &callbacks));
	CHECK(hrc_dmr_fm_rx_hook(NULL));
	CHECK(hrc_dmr.tx_begin(DMR_TIMING_OWN, 1, DMR_ACCESS_IMPOLITE));
	CHECK(!hrc_air.open(&profile, air_event_test, &callbacks));
	hrc_dmr.tx_abort();
	CHECK(!hrc_air.open(&profile, air_event_test, &callbacks)); // abort is deferred until the engine stops
	hrc_dmr_tx_drop();
	poll_on_resume = true; // try the deferred task at the first possible preemption
	CHECK(hrc_air.open(&profile, air_event_test, &callbacks));
	CHECK(air_on && !eng.running && scheduler_depth == 0 && modem_mask == 0);
	CHECK(((uintptr_t)AIR_SM % _Alignof(air_softmodem_t)) == 0);
	CHECK(!hrc_dmr.start(&cfg, dmr_event, NULL));
	CHECK(!hrc_air.open(&profile, air_event_test, &callbacks));
	CHECK(!hrc_dmr_fm_rx_hook(fm_half));
	CHECK(!hrc_baseband.rx_start());
	dmr_tx_burst_t burst = { 0 };
	CHECK(!hrc_dmr.tx_begin(DMR_TIMING_OWN, 1, DMR_ACCESS_IMPOLITE));
	CHECK(!hrc_dmr.tx_queue(&burst) && hrc_dmr.tx_queue_space() == 0);
	CHECK(hrc_dmr.tx_fill(&burst) == -1 && !hrc_dmr.is_transmitting());
#if CONFIG_HOTSPOT_SPLIT
	dmr_bs_config_t bs_cfg = { .cc = 1 };
	dmr_cach_t cach = { 0 };
	uint32_t counter, stamp;
	int8_t ts;
	CHECK(!hrc_dmr.bs_start(&bs_cfg));
	CHECK(!hrc_dmr.slot_clock(&counter, &stamp, &ts));
	CHECK(!hrc_dmr.bs_queue(1, &burst, &cach, NULL));
	CHECK(!hrc_dmr.bs_queue_at(0, 1, &burst, &cach, NULL));
	CHECK(hrc_dmr.bs_space(1) == 0 && !hrc_dmr.bs_keyed());
	CHECK(!hrc_dmr.bs_restart(0, 1));
	hrc_dmr.bs_key(true); hrc_dmr.bs_key(false); hrc_dmr.bs_stop();
#endif
	hrc_dmr.configure(&cfg); hrc_dmr.rx_restart(); hrc_dmr.rx_lock_slot(1);
	hrc_dmr.tx_listen(); hrc_dmr.tx_end(); hrc_dmr.tx_abort(); hrc_dmr.set_cc_filter(4);
	CHECK(AIR->open && !hrc_air.tx_begin()); // no audio isolation gate, even for TX-capable hardware
	uint32_t ticks = eng.st.ticks;
	in_irq = true;
	clock_us += 30000; handlers[IRQ_MODEM_TS_TX]();
	clock_us += 30000; handlers[IRQ_MODEM_TS_RX]();
	regs[C7_FM_ADDR_SW / 4] = 1;
	regs[C7_SYS_INTERP_LIST / 4] = C7_SYS_FM_RX;
	handlers[IRQ_MODEM_SYS]();
	regs[C7_SYS_INTERP_LIST / 4] = 0;
	in_irq = false;
	CHECK(eng.st.ticks == ticks && AIR->st.halves == 1);
	hrc_air.close();
	CHECK(!air_on && eng.running && scheduler_depth == 0 && modem_mask == 0);
	CHECK(eng.evq_head == eng.evq_tail && eng.txq_head == eng.txq_tail);
	hrc_air.close();
	CHECK(hrc_baseband.rx_start());
	CHECK(!hrc_air.open(&profile, air_event_test, &callbacks));
	int16_t samples[256];
	CHECK(hrc_baseband.rx_read(samples, 256) == 0);
	hrc_air.close(); CHECK(air_on && AIR->tap);
	hrc_baseband.rx_stop(); hrc_baseband.rx_stop();
	CHECK(!air_on && eng.running);

	brd = &isolated;
	CHECK((hrc_air.fit(&profile) & AIR_FIT_TX) != 0);
	CHECK(hrc_air.open(&profile, air_event_test, &callbacks));
	gate_allowed = false;
	CHECK(!hrc_air.tx_begin() && gate_calls == 1 && releases == 0);
	gate_allowed = true;
	CHECK(!hrc_air.tx_begin()); // empty TX queue: undo the acquired audio gate
	CHECK(gate_calls == 2 && releases == 1 && !air_tx_gated);
	air_tx_chunk_t chunk = { .nsyms = 144 };
	CHECK(hrc_air.tx_queue(&chunk));
	CHECK(hrc_air.tx_begin() && air_tx_gated);
#ifdef MODEM_TEST_PCM
	CHECK((dev.audio_ctl & C7_AUDIO_RING_PLAY) == 0);
#endif
	CHECK(!hrc_air.tx_begin());
	in_irq = true; handlers[IRQ_MODEM_RF_TX](); in_irq = false;
	CHECK(rf_tx && hrc_dmr_tx_active());
	hrc_air.tx_abort();
	CHECK(!rf_tx && air_tx_gated && releases == 1);
	CHECK(!hrc_air.tx_begin()); // the queued completion must not ungate a later transmission
	modem_poll_once();
	CHECK(callbacks > 0 && !air_tx_gated && releases == 2);
#ifdef MODEM_TEST_PCM
	CHECK((dev.audio_ctl & C7_AUDIO_RING_PLAY) != 0);
#endif
	CHECK(hrc_air.tx_queue(&chunk) && hrc_air.tx_begin());
	in_irq = true; handlers[IRQ_MODEM_RF_TX](); in_irq = false;
	CHECK(rf_tx);
	int before_close = callbacks;
	hrc_air.close();
	CHECK(!rf_tx && !air_tx_gated && releases == 3);
	CHECK(callbacks == before_close);
	int prior_pa = pa_calls;
	handlers[IRQ_TIMER3](); // an already-pending timer must not rekey a closed session
	CHECK(pa_calls == prior_pa);
	CHECK(hrc_air.open(&profile, air_event_test, &callbacks));
	modem_poll_once(); CHECK(callbacks == before_close);
	CHECK(hrc_air.tx_queue(&chunk) && hrc_air.tx_begin());
	in_irq = true; handlers[IRQ_MODEM_RF_TX](); in_irq = false;
	hrc_dmr_leave();
	CHECK(!active && AIR->suspended && !hrc_air.tx_begin());
	CHECK(!rf_tx && air_tx_gated && releases == 3);
	hrc_dmr_enter();
	CHECK(active && air_on && !AIR->suspended && !eng.running);
	CHECK(!hrc_air.tx_begin());
	modem_poll_once(); CHECK(!air_tx_gated && releases == 4);
	hrc_dmr_leave(); hrc_air.close();
	CHECK(!air_on && !eng.running);
	hrc_dmr_enter();
	CHECK(eng.running && scheduler_depth == 0 && modem_mask == 0);
	CHECK(resumes > 0);
#else
	(void)isolated;
	(void)poll_on_resume;
	(void)gate_calls; (void)releases; (void)resumes;
	CHECK(hrc_dmr_fm_rx_hook(fm_half));
	CHECK(hrc_dmr_fm_rx_hook(NULL));
#endif
	CHECK(hrc_dmr.start(&cfg, dmr_event, NULL));
	int before_dmr_pa = pa_calls;
	CHECK(hrc_dmr.tx_begin(DMR_TIMING_OWN, 1, DMR_ACCESS_IMPOLITE));
	handlers[IRQ_TIMER3](); CHECK(pa_calls == before_dmr_pa + 1);
	hrc_dmr_tx_drop();
	handlers[IRQ_TIMER3](); CHECK(pa_calls == before_dmr_pa + 1);
	eng.evq[eng.evq_head].ev = (dmr_event_t){ .kind = DMR_EV_SLOT_TICK };
	eng.evq_head = (uint8_t)((eng.evq_head + 1) % HRC7000_DMR_EVQ_DEPTH);
	int prior_cb = callbacks;
	modem_poll_once(); CHECK(callbacks > prior_cb);
	hrc_dmr.stop(); hrc_dmr_leave();
	CHECK(!active && !eng.running && !cpu_masked);
	printf("modemGlueTest: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
