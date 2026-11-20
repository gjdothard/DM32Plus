// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Existing state-machine fixture supplies decoded kinds; P25 engines are absent from this build.
#define main features_component_main
#include "radioFeaturesTest.c"
#undef main
#include "codeplug/codeplug_tag.h"
int main(void)
{
	start();
	chans[1].kind = CODEPLUG_KIND_P25;
	radio_sm_init(&sm, now);
	CHECK(sm.view.ch.mode == RADIO_MODE_NONE && !sm.dmr_running);
	req(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_MODE && hw.tx_calls == 0);
	req(RREQ_PTT, 0);
	req(RREQ_POWER_OFF, 0);
	CHECK(sm.power_off_req && !sm.power_off_busy);
	req(RREQ_PTT, 0);
	chans[1].kind = CODEPLUG_KIND_P25_TRUNK;
	radio_sm_init(&sm, now);
	CHECK(sm.view.ch.mode == RADIO_MODE_NONE && !sm.dmr_running);
	req(RREQ_PTT, 1);
	CHECK(sm.view.tx_deny == TXDENY_MODE && hw.tx_calls == 0);
	printf("radioP25OffTest: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
