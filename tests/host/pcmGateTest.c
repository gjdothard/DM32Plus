// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Execute the actual PCM worker and APRS route on fake hardware; no task runs
#include <stdio.h>
#include <string.h>
#include "audio_host.h"
#include "../../platform/hrc7000/audio_pcm.c"
#include "hrc_afsk.h"

static int checks, failures, cb_calls, route_calls;
static bool masked, task_ok = true, route_allowed = true, fifo_full;
static bool inside_irq, timers[7];
static uint32_t timer_period[7], now_us;
static hrc_irq_fn irqs[64];
static bool enabled[64];
static uint32_t regs[0x600 / 4];
static uint8_t ram[0xA00];
static hrc7000_t dev;
static int context = 0x1234;
static int new_context = 0x4321;
enum { NORMAL, ROUTE_ON, GATE, GATE_RELEASE, RESTART, MAKE_ROOM, LONG_CALLBACK };
static int action;
#if CONFIG_AIR
static void model_tick(void);
#endif
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)

uint32_t audio_host_irq_save(void) { bool prior = masked; masked = true; return prior; }
void audio_host_irq_restore(uint32_t saved)
{
	// FIFO-full is read-only hardware status; AUDIO_CONTROL writes cannot clear it
	if (fifo_full)
		regs[C7_AUDIO_CONTROL / 4] |= C7_AUDIO_RING_FULL;
	else
		regs[C7_AUDIO_CONTROL / 4] &= ~C7_AUDIO_RING_FULL;
	masked = saved != 0;
}
bool audio_host_in_isr(void) { return inside_irq; }
void audio_host_barrier(void) {}
void audio_host_timer_ack(uint8_t timer) { (void)timer; }
void hrc_irq_register(uint8_t n, hrc_irq_fn fn) { irqs[n] = fn; }
void hrc_irq_enable(uint8_t n, bool on) { enabled[n] = on; }
void hrc_timer_periodic(uint8_t n, uint32_t us) { timers[n] = true; timer_period[n] = us; }
void hrc_timer_stop(uint8_t n) { timers[n] = false; }
uint32_t hrc_micros(void) { return now_us; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, uint32_t stack, void *ctx,
		      uint32_t priority, TaskHandle_t *handle)
{
	(void)fn; (void)name; (void)stack; (void)ctx; (void)priority;
	*handle = task_ok ? (void *)1 : NULL;
	return task_ok ? pdPASS : pdFALSE;
}
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks) { (void)clear; (void)ticks; return 0; }
void vTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t *woken) { (void)t; *woken = pdFALSE; }
void xTaskNotifyGive(TaskHandle_t t) { (void)t; }

static void board_ring(bool on)
{
#if CONFIG_AIR
	hrc_audio_pcm_ring(&dev, on);
#else
	hrc7000_audio_bits(&dev, on ? C7_AUDIO_RING_PLAY : 0, on ? 0 : C7_AUDIO_RING_PLAY);
#endif
}

static void produce(const int16_t *mic, int16_t *speaker, void *ctx)
{
	CHECK(!masked && !inside_irq && mic[0] == 77);
	cb_calls++;
	for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
		speaker[i] = (int16_t)*(int *)ctx;
	if (action == ROUTE_ON)
		hrc_audio_pcm.speaker_pcm(true);
#if CONFIG_AIR
	if (action == LONG_CALLBACK)
		for (int tick = 0; tick < 30; tick++)
			model_tick();
	if (action == GATE || action == GATE_RELEASE) {
		CHECK(hrc_audio_pcm_air_gate());
		if (action == GATE_RELEASE)
			hrc_audio_pcm_air_release();
	}
#endif
	if (action == RESTART) {
		hrc_audio_pcm.stop();
		CHECK(hrc_audio_pcm.start(produce, &new_context));
	}
	if (action == MAKE_ROOM)
		spk_tail = (uint8_t)((spk_tail + 1u) % SPK_BLOCKS);
}

static void block(void)
{
	for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
		mic_blk[0][i] = 77;
	mic_ready = 0;
	audio_poll_once();
}

#if CONFIG_AIR
static unsigned fifo_samples, model_chunks, model_underflows;
static bool model_started;

static unsigned queued_samples(void)
{
	return (unsigned)((spk_head + SPK_BLOCKS - spk_tail) % SPK_BLOCKS) * AUDIO_BLOCK_SAMPLES - spk_pos;
}

static void refill_interrupt(void)
{
	bool prior_mask = masked;
	inside_irq = true;
	masked = true;
	irqs[IRQ_TIMER6]();
	masked = prior_mask;
	inside_irq = false;
}

static void model_tick(void)
{
	now_us += 1000;
	// Two 32-sample hardware halves, consumed at 8 samples/ms
	if (fifo_samples < 8) {
		if (model_started)
			model_underflows++;
		fifo_samples = 0;
	} else {
		fifo_samples -= 8;
	}
	fifo_full = fifo_samples > 32;
	audio_host_irq_restore(masked);
	unsigned before = queued_samples();
	int callbacks_before = cb_calls;
	if (timers[TIMER_PCM_REFILL] && enabled[IRQ_TIMER6])
		refill_interrupt();
	unsigned pushed = before - queued_samples();
	CHECK(pushed == 0 || pushed == 32); // bounded ISR, including a full FIFO
	CHECK(cb_calls == callbacks_before);
	fifo_samples += pushed;
	if (pushed) {
		model_started = true;
		model_chunks++;
	}
	CHECK(fifo_samples <= 64);
}
#endif

static bool aprs_route(bool on)
{
	route_calls++;
	if (on && !route_allowed)
		return false;
	board_ring(on);
	return true;
}

int main(void)
{
#if CONFIG_AIR
	CHECK(!hrc_audio_pcm_air_gate());
	hrc_audio_pcm_air_release();
#endif
	hrc7000_attach(&dev, regs, ram);
	hrc_audio_pcm_attach(&dev, board_ring);
	task_ok = false;
	CHECK(!hrc_audio_pcm.start(produce, &context));
	task_ok = true;
	CHECK(hrc_audio_pcm.start(produce, &context));
	CHECK(running && enabled[IRQ_PCM_WR] && irqs[IRQ_PCM_WR] != NULL);
	CHECK((dev.audio_ctl & C7_AUDIO_PCM_MIC) != 0);
	fifo_full = true;
	regs[C7_AUDIO_CONTROL / 4] |= C7_AUDIO_RING_FULL;
	action = ROUTE_ON;
	block(); // route-on inside the callback must retain its first block
	CHECK(spk_head != spk_tail && spk_q[spk_tail][0] == context);
	CHECK((dev.audio_ctl & C7_AUDIO_RING_PLAY) != 0);
	action = NORMAL;
	CHECK(irqs[IRQ_PCM_WR] != NULL);
#if CONFIG_AIR
	CHECK(irqs[IRQ_TIMER6] != NULL && enabled[IRQ_TIMER6]);
	CHECK(timers[TIMER_PCM_REFILL] && timer_period[TIMER_PCM_REFILL] == 1000);
	timers[TIMER_PA] = timers[TIMER_BOARD] = true; // owners this stream must not stop
#endif
	CHECK(hrc_audio_pcm_hold(true));
#if CONFIG_AIR
	CHECK(!hrc_audio_pcm_air_gate());
	CHECK(!timers[TIMER_PCM_REFILL] && !enabled[IRQ_TIMER6]);
#endif
	CHECK(hrc_audio_pcm_hold(false));
	static const hrc_afsk_board_t aprs = { .tx_route = aprs_route, .amp_per_level = 100 };
	uint8_t tones = 0x55;
	hrc_afsk_attach(&dev, &aprs);
#if CONFIG_AIR
	CHECK(hrc_audio_pcm_air_gate());
	CHECK(!hrc_audio_pcm_air_gate());
	CHECK(air_gated && spk_head == spk_tail);
	CHECK(!timers[TIMER_PCM_REFILL] && timers[TIMER_PA] && timers[TIMER_BOARD]);
	CHECK((dev.audio_ctl & C7_AUDIO_RING_PLAY) == 0);
	CHECK((dev.audio_ctl & C7_AUDIO_PCM_MIC) != 0 && running);
	hrc_audio_pcm.speaker_pcm(true);
	CHECK((dev.audio_ctl & C7_AUDIO_RING_PLAY) == 0);
	int before = route_calls;
	CHECK(!hrc_afsk.tx_start(&tones, 8, 1200, 1200, 2200, 50));
	CHECK(route_calls == before && !fifo_held);
	uint8_t sentinel[64];
	memcpy(sentinel, ram + C7_PROMPT_FIFO, sizeof sentinel);
	fifo_full = false;
	regs[C7_AUDIO_CONTROL / 4] &= ~C7_AUDIO_RING_FULL;
	block(); // keep consuming microphone blocks while speaker output is discarded
	CHECK(spk_head == spk_tail && cb_calls == 2);
	CHECK(memcmp(sentinel, ram + C7_PROMPT_FIFO, sizeof sentinel) == 0);
	hrc_audio_pcm_air_release();
	CHECK(!air_gated && (dev.audio_ctl & C7_AUDIO_RING_PLAY) != 0);
	hrc_audio_pcm_air_release();
	CHECK(spk_head == spk_tail);
	CHECK(hrc_audio_pcm_air_gate());
	board_ring(false);
	hrc_audio_pcm_air_release();
	CHECK((dev.audio_ctl & C7_AUDIO_RING_PLAY) == 0); // restore latest request, not a saved old bit
	board_ring(true);
	fifo_full = true;
	regs[C7_AUDIO_CONTROL / 4] |= C7_AUDIO_RING_FULL;
	action = GATE;
	block();
	CHECK(air_gated && spk_head == spk_tail);
	hrc_audio_pcm_air_release();
	action = GATE_RELEASE;
	block();
	CHECK(!air_gated && spk_head == spk_tail); // no block from before a gate edge
	action = RESTART;
	block();
	CHECK(spk_head == spk_tail && running && block_ctx == &new_context);
	action = NORMAL;
	block(); CHECK(spk_q[spk_tail][0] == new_context);
	spk_head = 2; spk_tail = 0; spk_pos = 0;
	action = MAKE_ROOM;
	block(); CHECK(spk_head == 0 && spk_tail == 1); // space became available during encode
	action = NORMAL;

	// Prime, settle across a clock wrap, then drain queued audio during a 30 ms encode
	hrc_audio_pcm.stop();
	CHECK(!timers[TIMER_PCM_REFILL]);
	CHECK(hrc_audio_pcm.start(produce, &context));
	hrc_audio_pcm.speaker_pcm(true);
	block();
	fifo_full = false; audio_host_irq_restore(masked);
	unsigned pending = queued_samples();
	for (int i = 0; i < 4; i++) {
		now_us += 1000;
		refill_interrupt();
	}
	CHECK(queued_samples() == pending); // one block alone does not start playback
	block();
	now_us = UINT32_MAX - 99u;
	refill_interrupt();
	pending = queued_samples();
	now_us += 100; refill_interrupt();
	CHECK(queued_samples() == pending);
	now_us += 100; refill_interrupt();
	CHECK(pending - queued_samples() == 32); // wrap-safe 200 us minimum, no busy wait
	hrc_audio_pcm.stop();
	CHECK(hrc_audio_pcm.start(produce, &context));
	fifo_full = true; audio_host_irq_restore(masked);
	block(); block();
	now_us = 0;
	for (int i = 0; i < 4; i++)
		model_tick();
	unsigned chunks_before = model_chunks;
	action = LONG_CALLBACK;
	block();
	CHECK(model_chunks > chunks_before && model_underflows == 0);
	CHECK(spk_head != spk_tail && timers[TIMER_PCM_REFILL]);
	action = NORMAL;
	CHECK(hrc_audio_pcm.spk_pending());
	for (int i = 0; i < 60 && queued_samples(); i++)
		model_tick();
	CHECK(queued_samples() == 0 && hrc_audio_pcm.spk_pending());
	now_us += 8000;
	CHECK(!hrc_audio_pcm.spk_pending());
	int saved_context = context;
	context = 0;
	block(); // silence keeps the stream alive without holding a finished call open
	CHECK(spk_head != spk_tail && !hrc_audio_pcm.spk_pending());
	context = saved_context;
	block();
	CHECK(hrc_audio_pcm.spk_pending());
	CHECK(hrc_audio_pcm_air_gate());
	CHECK(!hrc_audio_pcm.spk_pending());
	uint8_t last_fifo[64]; memcpy(last_fifo, ram + C7_PROMPT_FIFO, sizeof last_fifo);
	refill_interrupt(); // a pending interrupt after the timer stopped must not push
	CHECK(memcmp(last_fifo, ram + C7_PROMPT_FIFO, sizeof last_fifo) == 0);
	hrc_audio_pcm_air_release();
#endif
	CHECK(hrc_afsk.tx_start(&tones, 8, 1200, 1200, 2200, 50));
	CHECK(fifo_held);
#if CONFIG_AIR
	CHECK(!hrc_audio_pcm_air_gate());
#endif
	CHECK(hrc_afsk.tx_busy());
#if CONFIG_AIR
	CHECK(!timers[TIMER_PCM_REFILL] && timers[TIMER_AFSK]);
#endif
	hrc_afsk.tx_stop();
	CHECK(!hrc_afsk.tx_busy() && !fifo_held);
	route_allowed = false;
	CHECK(!hrc_afsk.tx_start(&tones, 8, 1200, 1200, 2200, 50));
	CHECK(!fifo_held);
#if CONFIG_AIR
	CHECK(hrc_audio_pcm_air_gate());
	CHECK(!hrc_audio_pcm_hold(true));
	hrc_audio_pcm.speaker_pcm(false);
	hrc_audio_pcm_air_release();
	CHECK((dev.audio_ctl & C7_AUDIO_RING_PLAY) == 0);
#endif
	hrc_audio_pcm.stop();
	CHECK(!running && !masked);
#if CONFIG_AIR
	CHECK(!timers[TIMER_PCM_REFILL] && !enabled[IRQ_TIMER6]);
	CHECK(timers[TIMER_PA] && timers[TIMER_BOARD]);
#endif
	printf("pcmGateTest: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
