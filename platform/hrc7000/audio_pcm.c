// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Garrett Dothard
// HR_C7000 PCM audio (audio_pcm.h): mic samples from the modem's PCM buffer, speaker samples to its prompt FIFO
// (docs/spec/hardware-dm32.md 9; modem-hrc7000.md 3, 8.1, 8.4).
//
// With AUDIO_CONTROL b4 set the modem fills 80 mic samples at 0x18000000 every 10 ms and raises PCM_wr (PIC 28). The
// mic interrupt paces everything: two halves make a 20 ms block, the task runs the block callback (never in the ISR)
// and queues the speaker block it returned. While the speaker is routed (AUDIO_CONTROL b7, set by the board's hook)
// a dedicated timer pushes the queue with CONFIG_AIR; otherwise the task polls the FIFO. Both push 32 samples when
// b31 says there is room. The
// CPU-fed speaker buffer (b5, 0x180000A0) sounded rough and choppy on the bench with every sample layout tried.
//
// Sample format: mic buffer 16-bit native words; prompt FIFO 16-bit samples, low byte first (bench).
//
// Mic level for VOX (CONFIG_VOX, hrc_audio_pcm_mic_level): these radios have no analog mic level tap, so the level is
// measured on the same mic PCM. The first call turns the PCM interrupt on without the stream (the meter), and the
// interrupt turns it off again once nobody has asked for METER_IDLE_BLOCKS halves. The stream and the meter share
// AUDIO_CONTROL b4 and the interrupt: each turns them off only when the other isn't using them.
#include <string.h>
#include "config.h"
#include "audio_pcm.h"

#if CONFIG_DMR	// the whole file: DMR only
#include "platform.h"
#include "FreeRTOS.h"
#include "task.h"
#if CONFIG_VOX
#include "audio/miclevel/miclevel.h"
#endif

#define AUDIO_TASK_PRIORITY	5	// above the radio task: a late block is an audible gap
#define AUDIO_TASK_STACK	1536	// words: the vocoder in the callback
#define HALF			C7_PCM_SAMPLES	// 80
#define SPK_BLOCKS		3

static int16_t mic_blk[2][AUDIO_BLOCK_SAMPLES];	// double buffered: the ISR fills one while the task reads the other
static int16_t spk_q[SPK_BLOCKS][AUDIO_BLOCK_SAMPLES];
static volatile uint8_t spk_head, spk_tail;	// task writes head, ISR reads tail
static uint8_t spk_pos;			// samples of spk_q[spk_tail] already pushed
static volatile uint8_t mic_fill, mic_half;
static volatile int8_t mic_ready = -1;		// block index ready for the task

static hrc7000_t *modem;
static void (*speaker_pcm_cb)(bool on);
static TaskHandle_t task;
static audio_block_cb_t block_cb;
static void *block_ctx;
static volatile bool running;
static bool spk_routed;		// the last speaker_pcm() state
static volatile bool fifo_held;	// another user (the board's AFSK) owns the FIFO: speaker PCM waits
static volatile uint32_t blocks, overruns, underruns;	// underruns: the FIFO does not report them (stays 0)
static bool isr_registered;
#if CONFIG_AIR
static volatile bool air_gated;
static bool ring_want;
static uint32_t spk_generation;
#define PCM_REFILL_US	1000u
#define PCM_SETTLE_US	200u	// defer across ticks after observing room; never spin in the interrupt
#define PCM_PRIME_BLOCKS	2u
_Static_assert(TIMER_PCM_REFILL != TIMER_TICK && TIMER_PCM_REFILL != TIMER_CLOCK
	       && TIMER_PCM_REFILL != TIMER_PA && TIMER_PCM_REFILL != TIMER_AFSK
	       && TIMER_PCM_REFILL != TIMER_BOARD, "PCM refill timer conflicts with another owner");
static bool refill_active, refill_room_seen, spk_primed;
static uint32_t refill_room_at;
static bool spk_loud[SPK_BLOCKS];
static volatile bool loud_recent;
static volatile uint32_t loud_pushed;
#define PCM_FIFO_PLAY_US	8000u	// two 32-sample halves at 8 kHz
static void refill_update(void);

static void refill_reset(void)
{
	refill_room_seen = false;
	spk_primed = false;
	loud_recent = false;
}

static void speaker_drop(void)
{
	spk_head = spk_tail = 0;
	spk_pos = 0;
	spk_generation++;
	refill_reset();
}

static void ring_apply(void)
{
	bool on = ring_want && !air_gated;
	hrc7000_audio_bits(modem, on ? C7_AUDIO_RING_PLAY : 0, on ? 0 : C7_AUDIO_RING_PLAY);
}

void hrc_audio_pcm_ring(hrc7000_t *m, bool on)
{
	uint32_t saved = hrc_irq_save();
	modem = m;
	ring_want = on;
	ring_apply();
	hrc_irq_restore(saved);
}

bool hrc_audio_pcm_air_gate(void)
{
	uint32_t saved = hrc_irq_save();
	if (modem == NULL || fifo_held || air_gated) {
		hrc_irq_restore(saved);
		return false;
	}
	air_gated = true;
	speaker_drop();
	ring_apply();
	refill_update();
	hrc_irq_restore(saved);
	return true;
}

void hrc_audio_pcm_air_release(void)
{
	uint32_t saved = hrc_irq_save();
	if (air_gated) {
		speaker_drop();
		air_gated = false;
		ring_apply();
		refill_update();
	}
	hrc_irq_restore(saved);
}
#endif

#if CONFIG_VOX
#define METER_IDLE_BLOCKS	100u	// halves (1 s) without a mic_level() call: the meter stops
static miclevel_t meter;
static volatile bool meter_on;
static volatile uint8_t meter_idle;	// halves since the last mic_level() call
#endif

static volatile int16_t *const pcm_mic = (volatile int16_t *)C7_PCM_MIC_ADDR;

static void isr_pcm_wr(void)
{
	hrc7000_wr(modem, C7_INTERP_CLEAR, C7_INT_PCM_WR);
#if CONFIG_VOX
	if (meter_on) {
		miclevel_block(&meter, pcm_mic, HALF);
		if (++meter_idle >= METER_IDLE_BLOCKS) {
			meter_on = false;	// nobody reads it (VOX off): the stream's use alone decides now
			if (!running) {
				hrc7000_audio_bits(modem, 0, C7_AUDIO_PCM_MIC);
				hrc_irq_enable(IRQ_PCM_WR, false);
			}
		}
	}
#endif
	if (!running) {
		return;
	}
	int16_t *dst = &mic_blk[mic_fill][mic_half * HALF];
	for (uint32_t i = 0; i < HALF; i++) {
		dst[i] = pcm_mic[i];
	}
	if (++mic_half < 2) {
		return;
	}
	mic_half = 0;
	if (mic_ready >= 0) {
		overruns++;	// the task hasn't taken the previous block: it is overwritten
	}
	mic_ready = (int8_t)mic_fill;
	mic_fill ^= 1u;
	if (task != NULL) {
		BaseType_t woken = pdFALSE;
		vTaskNotifyGiveFromISR(task, &woken);
		portYIELD_FROM_ISR(woken);
	}
}

// CONFIG_AIR uses a bounded timer refill so a codec callback cannot stall playback. Otherwise the task polls every
// 2 ms while the speaker is routed, faster than a chunk (4 ms) plays
static void fifo_feed(void)
{
#if CONFIG_AIR
	// One bounded push per timer interrupt; no callback or RTOS work here
	uint32_t saved = hrc_irq_save();
	if (!refill_active || !spk_routed || fifo_held || air_gated || spk_tail == spk_head) {
		refill_room_seen = false;
		hrc_irq_restore(saved);
		return;
	}
	if (!spk_primed) {
		uint8_t queued = (uint8_t)((spk_head + SPK_BLOCKS - spk_tail) % SPK_BLOCKS);
		if (queued < PCM_PRIME_BLOCKS) {
			hrc_irq_restore(saved);
			return;
		}
		spk_primed = true;
	}
	if ((hrc7000_rd(modem, C7_AUDIO_CONTROL) & C7_AUDIO_RING_FULL) != 0) {
		refill_room_seen = false;
		hrc_irq_restore(saved);
		return;
	}
	uint32_t now = hrc_micros();
	if (!refill_room_seen) {
		refill_room_at = now;
		refill_room_seen = true;
		hrc_irq_restore(saved);
		return;
	}
	if (now - refill_room_at >= PCM_SETTLE_US
	    && hrc7000_prompt_push(modem, &spk_q[spk_tail][spk_pos])) {
		if (spk_loud[spk_tail]) {
			loud_pushed = now;
			loud_recent = true;
		}
		refill_room_seen = false;
		spk_pos = (uint8_t)(spk_pos + C7_PROMPT_CHUNK);
		if (spk_pos >= AUDIO_BLOCK_SAMPLES) {
			spk_pos = 0;
			spk_tail = (uint8_t)((spk_tail + 1u) % SPK_BLOCKS);
		}
	}
	hrc_irq_restore(saved);
#else
	if (!spk_routed || fifo_held) {
		return;
	}
	while (spk_tail != spk_head && hrc7000_prompt_push(modem, &spk_q[spk_tail][spk_pos])) {
		spk_pos = (uint8_t)(spk_pos + C7_PROMPT_CHUNK);
		if (spk_pos >= AUDIO_BLOCK_SAMPLES) {
			spk_pos = 0;
			spk_tail = (uint8_t)((spk_tail + 1u) % SPK_BLOCKS);
		}
	}
#endif
}

#if CONFIG_AIR
static void isr_pcm_refill(void)
{
	hrc_timer_ack(TIMER_PCM_REFILL);
	fifo_feed();
}

// Called with CPU interrupts masked when stream, route, gate or FIFO ownership changes
static void refill_update(void)
{
	bool want = running && spk_routed && !fifo_held && !air_gated;
	if (want && !refill_active) {
		refill_room_seen = false;
		refill_active = true;
		hrc_timer_periodic(TIMER_PCM_REFILL, PCM_REFILL_US);
		hrc_irq_enable(IRQ_TIMER6, true);
	} else if (!want && refill_active) {
		hrc_timer_stop(TIMER_PCM_REFILL);
		hrc_irq_enable(IRQ_TIMER6, false);
		refill_active = false;
		refill_room_seen = false;
	}
}

static bool speaker_pending(void)
{
	uint32_t saved = hrc_irq_save();
	bool pending = false;
	for (uint8_t i = spk_tail; i != spk_head; i = (uint8_t)((i + 1u) % SPK_BLOCKS))
		pending |= spk_loud[i];
	if (loud_recent && hrc_micros() - loud_pushed >= PCM_FIFO_PLAY_US)
		loud_recent = false;
	pending |= loud_recent;
	hrc_irq_restore(saved);
	return pending;
}
#endif

static void audio_poll_once(void)
{
	static int16_t mic[AUDIO_BLOCK_SAMPLES];
#if !CONFIG_AIR
	fifo_feed();
#endif
	uint32_t s = hrc_irq_save();
	int8_t idx = mic_ready;
	if (idx >= 0) {
		memcpy(mic, mic_blk[idx], sizeof mic);
		mic_ready = -1;
	}
	audio_block_cb_t cb = block_cb;
#if CONFIG_AIR
	void *ctx = block_ctx;
	uint32_t generation = spk_generation;
#endif
	hrc_irq_restore(s);
	if (idx < 0 || !running || cb == NULL) {
		return;
	}
#if CONFIG_AIR
	// Separate callback storage survives a queue reset or route change during encode
	static int16_t speaker[AUDIO_BLOCK_SAMPLES];
	cb(mic, speaker, ctx);
	bool loud = false;
	for (uint16_t i = 0; i < AUDIO_BLOCK_SAMPLES && !loud; i++)
		loud = speaker[i] != 0;
	s = hrc_irq_save();
	uint8_t next = (uint8_t)((spk_head + 1u) % SPK_BLOCKS);
	if (running && block_cb == cb && block_ctx == ctx && generation == spk_generation
	    && !air_gated && spk_routed && next != spk_tail) {
		memcpy(spk_q[spk_head], speaker, sizeof speaker);
		spk_loud[spk_head] = loud;
		spk_head = next;
	}
	hrc_irq_restore(s);
#else
	uint8_t next = (uint8_t)((spk_head + 1u) % SPK_BLOCKS);
	if (next == spk_tail) {
		static int16_t discard[AUDIO_BLOCK_SAMPLES];
		cb(mic, discard, block_ctx);	// the speaker side is full (PCM not routed): keep the mic side going
	} else {
		cb(mic, spk_q[spk_head], block_ctx);
		spk_head = next;
	}
#endif
	blocks++;
#if !CONFIG_AIR
	fifo_feed();
#endif
}

static void audio_task(void *arg)
{
	(void)arg;
	for (;;) {
#if CONFIG_AIR
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#else
		ulTaskNotifyTake(pdTRUE, spk_routed ? pdMS_TO_TICKS(2) : portMAX_DELAY);
#endif
		audio_poll_once();
	}
}

void hrc_audio_pcm_attach(hrc7000_t *m, void (*speaker_pcm)(bool on))
{
	modem = m;
#if CONFIG_AIR
	ring_want = (m->audio_ctl & C7_AUDIO_RING_PLAY) != 0;
#endif
	speaker_pcm_cb = speaker_pcm;
	// The meter's interrupt changes AUDIO_CONTROL b4 (isr_pcm_wr): from now on every shadow update, the board's FM
	// receive, transmit, idle and DMR entry included, runs with interrupts off
	hrc7000_set_crit(m, hrc_irq_save, hrc_irq_restore);
}

bool hrc_audio_pcm_hold(bool on)
{
#if CONFIG_AIR
	uint32_t saved = hrc_irq_save();
	if (on && air_gated) {
		hrc_irq_restore(saved);
		return false;
	}
#endif
	fifo_held = on;
#if CONFIG_AIR
	refill_room_seen = false;
	refill_update();
	hrc_irq_restore(saved);
#endif
	return true;
}

// Once: registering masks the source, so never while the meter or the stream has it on
static void isr_register(void)
{
	uint32_t s = hrc_irq_save();	// the stream (audio users' task) and the meter (radio task) can both get here first
	if (!isr_registered) {
		hrc_irq_register(IRQ_PCM_WR, isr_pcm_wr);
#if CONFIG_AIR
		hrc_irq_register(IRQ_TIMER6, isr_pcm_refill);
#endif
		isr_registered = true;
	}
	hrc_irq_restore(s);
}

static bool start(audio_block_cb_t cb, void *ctx)
{
	if (task == NULL) {
		isr_register();
		if (xTaskCreate(audio_task, "audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIORITY, &task) != pdPASS) {
			task = NULL;
			return false;
		}
	}
	uint32_t saved = hrc_irq_save();
	block_ctx = ctx;
	block_cb = cb;
	mic_fill = 0;
	mic_half = 0;
	mic_ready = -1;
	spk_head = spk_tail = 0;
	spk_pos = 0;
#if CONFIG_AIR
	spk_generation++;
	refill_reset();
#endif
	hrc_barrier();
	running = true;
	hrc7000_wr(modem, C7_INTERP_CLEAR, C7_INT_PCM_WR);
	hrc_irq_enable(IRQ_PCM_WR, true);
	hrc7000_audio_bits(modem, C7_AUDIO_PCM_MIC, 0);	// under the lock set at attach
#if CONFIG_AIR
	refill_update();
#endif
	hrc_irq_restore(saved);
	return true;
}

static void stop(void)
{
	uint32_t s = hrc_irq_save();	// the meter's interrupt decides on the same state
	running = false;
	block_cb = NULL;
#if CONFIG_AIR
	speaker_drop();
	refill_update();
#endif
#if CONFIG_VOX
	bool keep = meter_on;
#else
	bool keep = false;
#endif
	if (!keep) {
		hrc7000_audio_bits(modem, 0, C7_AUDIO_PCM_MIC);
		hrc_irq_enable(IRQ_PCM_WR, false);
	}
	hrc_irq_restore(s);
}

// Speaker PCM: the modem plays the CPU's samples instead of its FM demodulator (the board's hook owns the bits).
// Callers repeat the call every block (beep.c), from inside the block callback, so the queue starts empty only when
// the route turns on: emptying it on every call threw away the block being queued and the one playing (choppy beeps)
static void speaker_pcm(bool on)
{
	uint32_t saved = hrc_irq_save();	// serialize queue resets with the worker and TX gate
	if (on != spk_routed) {
#if CONFIG_AIR
		// Route-on can be requested inside the callback producing its first block
		if (!on)
			spk_generation++;
		refill_reset();
#endif
		spk_head = spk_tail = 0;
		spk_pos = 0;
	}
	spk_routed = on;
	if (speaker_pcm_cb != NULL) {
		speaker_pcm_cb(on);
	}
#if CONFIG_AIR
	refill_update();
#endif
	hrc_irq_restore(saved);
}

const hal_audio_t hrc_audio_pcm = {
	.start = start,
	.stop = stop,
	.speaker_pcm = speaker_pcm,
#if CONFIG_AIR
	.spk_pending = speaker_pending,
#endif
};

void hrc_audio_pcm_stats(uint32_t *b, uint32_t *ov, uint32_t *un)
{
	*b = blocks;
	*ov = overruns;
	*un = underruns;
}

void hrc_audio_pcm_reset_stats(void)
{
	blocks = overruns = underruns = 0;
}

#if CONFIG_VOX
// hal_power_t.mic_level. Task level (the radio task's VOX poll, the debug link). The first call after the meter
// stopped turns it on and returns 0: the level needs a block
uint8_t hrc_audio_pcm_mic_level(void)
{
	if (modem == NULL) {
		return 0;
	}
	meter_idle = 0;
	if (meter_on) {
		return meter.level;
	}
	isr_register();
	uint32_t s = hrc_irq_save();
	miclevel_reset(&meter);
	meter_idle = 0;
	meter_on = true;
	if (!running) {
		hrc7000_wr(modem, C7_INTERP_CLEAR, C7_INT_PCM_WR);	// a stale flag would count as a block at once
	}
	hrc_irq_enable(IRQ_PCM_WR, true);
	hrc7000_audio_bits(modem, C7_AUDIO_PCM_MIC, 0);
	hrc_irq_restore(s);
	return 0;
}

void hrc_audio_pcm_mic_stats(uint8_t *level, uint16_t *peak, uint32_t *halves, bool *on)
{
	*level = meter.level;
	*peak = meter.peak;
	*halves = meter.blocks;
	*on = meter_on;
}
#endif

#endif	// CONFIG_DMR
