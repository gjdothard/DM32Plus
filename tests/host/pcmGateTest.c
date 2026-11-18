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
static hrc_irq_fn irqs[64];
static bool enabled[64];
static uint32_t regs[0x600 / 4];
static uint8_t ram[0xA00];
static hrc7000_t dev;
static int context = 0x1234;
static int new_context = 0x4321;
enum { NORMAL, ROUTE_ON, GATE, GATE_RELEASE, RESTART, MAKE_ROOM };
static int action;
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
bool audio_host_in_isr(void) { return false; }
void audio_host_barrier(void) {}
void audio_host_timer_ack(uint8_t timer) { (void)timer; }
void hrc_irq_register(uint8_t n, hrc_irq_fn fn) { irqs[n] = fn; }
void hrc_irq_enable(uint8_t n, bool on) { enabled[n] = on; }
void hrc_timer_periodic(uint8_t n, uint32_t us) { (void)n; (void)us; }
void hrc_timer_stop(uint8_t n) { (void)n; }
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
	CHECK(!masked && mic[0] == 77);
	cb_calls++;
	for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
		speaker[i] = (int16_t)*(int *)ctx;
	if (action == ROUTE_ON)
		hrc_audio_pcm.speaker_pcm(true);
#if CONFIG_AIR
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
	CHECK(hrc_audio_pcm_hold(true));
#if CONFIG_AIR
	CHECK(!hrc_audio_pcm_air_gate());
#endif
	CHECK(hrc_audio_pcm_hold(false));
	static const hrc_afsk_board_t aprs = { .tx_route = aprs_route, .amp_per_level = 100 };
	uint8_t tones = 0x55;
	hrc_afsk_attach(&dev, &aprs);
#if CONFIG_AIR
	CHECK(hrc_audio_pcm_air_gate());
	CHECK(!hrc_audio_pcm_air_gate());
	CHECK(air_gated && spk_head == spk_tail);
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
#endif
	CHECK(hrc_afsk.tx_start(&tones, 8, 1200, 1200, 2200, 50));
	CHECK(fifo_held);
#if CONFIG_AIR
	CHECK(!hrc_audio_pcm_air_gate());
#endif
	CHECK(hrc_afsk.tx_busy());
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
	printf("pcmGateTest: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
