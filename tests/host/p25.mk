# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
# Copyright (c) 2026 The RadioFoundry Authors
# P25 foundation imported from RadioFoundry c34dc440. Radio operation awaits
# board/radio/codeplug integration; these test flags do not enable hardware TX.

P25_FEC := $(wildcard $(CORE)/fec/*.c)
P25_UNITS := $(CORE)/p25/p25_unit.c $(CORE)/p25/p25_rx.c $(CORE)/p25/p25_call.c
P25_FRAMER := $(CORE)/air/air_framer.c
P25_ENGINE := $(CORE)/p25/p25_engine.c $(CORE)/air/air_session.c $(P25_FRAMER) $(P25_UNITS) $(P25_FEC)
P25_MBE := $(wildcard $(CORE)/mbe/*.c)
P25_FLAGS := -I$(ROOT)/hal -Wshadow -DCONFIG_P25=1 -DCONFIG_IMBE=1 -DCONFIG_P25_TRUNK=0
P25_HEADERS := $(wildcard $(CORE)/p25/*.h $(CORE)/air/*.h $(CORE)/fec/*.h $(CORE)/mbe/*.h) \
	$(CORE)/dsp/fxp.h $(ROOT)/hal/hal_air.h $(ROOT)/hal/hal_baseband.h

TESTS += fecTest airFramerTest p25Test p25EngineTest p25EngineTestTrunk mbeTest imbeEncTest \
	p25TrunkTest p25VoiceTest fxpTest airSoftmodemTest boardAirTest hrc7000AirTest \
	modemGlueTest modemGlueTestNoAir modemGlueTestSplit modemGlueTestNoAirSplit
TESTS += pcmGateTest pcmGateTestNoAir pcmGateTestVox pcmGateTestNoAirVox modemGluePcmTest
TESTS += vocoderMemoryTest vocoderMemoryTestTrunk vocoderMemoryTestNoP25
TESTS += p25CodeplugTest p25CodeplugTestTrunk
TESTS += codeplugKvTest
TESTS += p25StoredTest p25StoredTestTrunk
TESTS += codeplugTagTest
TESTS += p25AdmissionTest p25AdmissionTestTrunk
TESTS += radioP25OwnerTest radioP25OwnerTestTrunk
TESTS += radioP25StateTest
TESTS += radioP25TrunkStateTest
TESTS += radioP25OffTest codeplugTagTestNoP25

P25_STORE_CORE := $(CORE)/codeplug/codeplug.c $(CORE)/codeplug/dmr_iddb.c $(CORE)/storage/storage.c \
	$(CORE)/p25/p25_favorites.c \
	$(CORE)/codeplug/codeplug_talk.c \
	$(CORE)/codeplug/codeplug_kv.c $(CORE)/codeplug/codeplug_tag.c $(CORE)/codeplug/codeplug_tagged.c \
	$(CORE)/codeplug/codeplug_records.c
P25_STORE_HEADERS := $(wildcard $(CORE)/codeplug/*.h)

radioP25OffTest: radioP25OffTest.c radioFeaturesTest.c $(filter-out radioFeaturesTest.c,$(RFTEST))
	$(CC) $(CFLAGS) -I$(ROOT)/hal -I. -DCONFIG_DMR_IP_GATEWAY=0 -DCONFIG_HOTSPOT=0 -DCONFIG_APRS=0 -DDMR_RX_TRACE=0 \
		-o $@ radioP25OffTest.c $(filter-out radioFeaturesTest.c,$(RFTEST)) -lm

codeplugTagTestNoP25: codeplugTagTest.c $(P25_STORE_CORE) $(P25_STORE_HEADERS) sim/sim_flash.c sim/sim_flash.h ../vectors/channel-modes.txt
	$(CC) $(CFLAGS) -I$(ROOT)/hal -DCONFIG_P25=0 -DCONFIG_P25_TRUNK=0 -o $@ codeplugTagTest.c \
		$(filter-out $(CORE)/codeplug/codeplug_tag.c $(CORE)/codeplug/codeplug_records.c,$(P25_STORE_CORE)) sim/sim_flash.c

P25_OWNER := radioP25OwnerTest.c $(CORE)/radio/radio_p25.c $(CORE)/p25/p25_workspace.c \
	$(CORE)/p25/p25_admission.c $(CORE)/p25/p25_codeplug.c $(CORE)/p25/p25Voice.c $(P25_ENGINE) $(P25_MBE) \
	$(P25_STORE_CORE) sim/sim_flash.c

P25_STATE := $(sort radioP25StateTest.c $(filter-out radioP25OwnerTest.c,$(P25_OWNER)) \
	$(filter-out radioFeaturesTest.c sim/codeplug_modes_sim.c,$(RFTEST)))
radioP25StateTest: $(P25_STATE) radioP25OwnerTest.c $(P25_HEADERS) $(P25_STORE_HEADERS) $(wildcard $(CORE)/radio/*.h)
	$(CC) $(CFLAGS) $(P25_FLAGS) -I. -DCONFIG_AM_RX=0 -DCONFIG_BCAST_FM=0 -DCONFIG_DMR_IP_GATEWAY=0 \
		-DCONFIG_HOTSPOT=0 -DCONFIG_APRS=0 -DCONFIG_NET_USRP=0 -DCONFIG_BEEP=0 -DCONFIG_SFR=0 -DDMR_RX_TRACE=0 \
		-o $@ $(P25_STATE) -lm
radioP25TrunkStateTest: radioP25TrunkStateTest.c radioP25StateTest.c $(P25_STATE) radioP25OwnerTest.c $(P25_HEADERS) $(P25_STORE_HEADERS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -UCONFIG_P25_TRUNK -DCONFIG_P25_TRUNK=1 -DCONFIG_DMR_TALKGROUP_ENTRIES=1 -I. -DCONFIG_AM_RX=0 -DCONFIG_BCAST_FM=0 \
		-DCONFIG_DMR_IP_GATEWAY=0 -DCONFIG_HOTSPOT=0 -DCONFIG_APRS=0 -DCONFIG_NET_USRP=0 -DCONFIG_BEEP=0 -DCONFIG_SFR=0 -DDMR_RX_TRACE=0 \
		-o $@ radioP25TrunkStateTest.c $(filter-out radioP25StateTest.c,$(P25_STATE)) $(CORE)/p25/p25_follow.c $(CORE)/p25/p25_tsbk.c -lm
radioP25OwnerTest: $(P25_OWNER) $(P25_HEADERS) $(P25_STORE_HEADERS) $(CORE)/radio/radio_p25.h
	$(CC) $(CFLAGS) $(P25_FLAGS) -DCONFIG_AM_RX=0 -DCONFIG_BCAST_FM=0 -o $@ $(P25_OWNER) -lm
radioP25OwnerTestTrunk: $(P25_OWNER) $(P25_HEADERS) $(P25_STORE_HEADERS) $(CORE)/radio/radio_p25.h $(CORE)/p25/p25_tsbk.c $(CORE)/p25/p25_follow.c
	$(CC) $(CFLAGS) $(P25_FLAGS) -UCONFIG_P25_TRUNK -DCONFIG_P25_TRUNK=1 -DCONFIG_AM_RX=0 -DCONFIG_BCAST_FM=0 \
		-o $@ $(P25_OWNER) $(CORE)/p25/p25_tsbk.c $(CORE)/p25/p25_follow.c -lm

P25_ADMISSION := p25AdmissionTest.c $(CORE)/p25/p25_admission.c $(CORE)/p25/p25_codeplug.c $(P25_STORE_CORE) sim/sim_flash.c
p25AdmissionTest: $(P25_ADMISSION) $(P25_HEADERS) $(P25_STORE_HEADERS) sim/sim_flash.h
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ $(P25_ADMISSION)
p25AdmissionTestTrunk: $(P25_ADMISSION) $(P25_HEADERS) $(P25_STORE_HEADERS) sim/sim_flash.h
	$(CC) $(CFLAGS) $(P25_FLAGS) -UCONFIG_P25_TRUNK -DCONFIG_P25_TRUNK=1 -o $@ $(P25_ADMISSION)

codeplugTagTest: codeplugTagTest.c $(P25_STORE_CORE) $(P25_STORE_HEADERS) sim/sim_flash.c sim/sim_flash.h ../vectors/channel-modes.txt
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ codeplugTagTest.c \
		$(filter-out $(CORE)/codeplug/codeplug_tag.c $(CORE)/codeplug/codeplug_records.c,$(P25_STORE_CORE)) sim/sim_flash.c

p25StoredTest: p25StoredTest.c $(P25_STORE_CORE) $(P25_STORE_HEADERS) $(CORE)/p25/p25_codeplug.c \
	$(CORE)/p25/p25_codeplug.h sim/sim_flash.c sim/sim_flash.h ../vectors/channel-modes.txt
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ p25StoredTest.c $(CORE)/p25/p25_codeplug.c $(P25_STORE_CORE) sim/sim_flash.c
p25StoredTestTrunk: p25StoredTest.c $(P25_STORE_CORE) $(P25_STORE_HEADERS) $(CORE)/p25/p25_codeplug.c \
	$(CORE)/p25/p25_codeplug.h sim/sim_flash.c sim/sim_flash.h ../vectors/channel-modes.txt
	$(CC) $(CFLAGS) $(P25_FLAGS) -UCONFIG_P25_TRUNK -DCONFIG_P25_TRUNK=1 -o $@ p25StoredTest.c \
		$(CORE)/p25/p25_codeplug.c $(P25_STORE_CORE) sim/sim_flash.c

codeplugKvTest: codeplugKvTest.c $(CORE)/codeplug/codeplug_kv.c $(CORE)/codeplug/codeplug_kv.h \
	$(CORE)/codeplug/codeplug.c $(CORE)/codeplug/dmr_iddb.c $(CORE)/storage/storage.c sim/sim_flash.c sim/sim_flash.h
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ codeplugKvTest.c $(CORE)/codeplug/codeplug_kv.c \
		$(CORE)/codeplug/codeplug.c $(CORE)/codeplug/dmr_iddb.c $(CORE)/storage/storage.c sim/sim_flash.c

p25CodeplugTest: p25CodeplugTest.c $(CORE)/p25/p25_codeplug.c $(P25_STORE_CORE) $(P25_STORE_HEADERS) \
	sim/p25_link_sim.c $(CORE)/p25/p25_codeplug.h ../vectors/p25-codeplug.txt
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ p25CodeplugTest.c $(CORE)/p25/p25_codeplug.c $(P25_STORE_CORE) sim/p25_link_sim.c
p25CodeplugTestTrunk: p25CodeplugTest.c $(CORE)/p25/p25_codeplug.c $(P25_STORE_CORE) $(P25_STORE_HEADERS) \
	sim/p25_link_sim.c $(CORE)/p25/p25_codeplug.h ../vectors/p25-codeplug.txt
	$(CC) $(CFLAGS) $(P25_FLAGS) -UCONFIG_P25_TRUNK -DCONFIG_P25_TRUNK=1 -o $@ p25CodeplugTest.c \
		$(CORE)/p25/p25_codeplug.c $(P25_STORE_CORE) sim/p25_link_sim.c

P25_MEMORY := vocoderMemoryTest.c $(CORE)/p25/p25_workspace.c
P25_MEMORY_DEPS := $(ROOT)/platform/hrc7000/vocoder.c $(P25_HEADERS) $(ROOT)/hal/hal_vocoder.h \
	$(wildcard $(ROOT)/platform/hrc7000/*.h stubs/modem_rtos/*.h)
vocoderMemoryTest: $(P25_MEMORY) $(P25_MEMORY_DEPS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -I$(ROOT)/platform/hrc7000 -Istubs/modem_rtos -o $@ $(P25_MEMORY)
vocoderMemoryTestTrunk: $(P25_MEMORY) $(P25_MEMORY_DEPS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -UCONFIG_P25_TRUNK -DCONFIG_P25_TRUNK=1 \
		-I$(ROOT)/platform/hrc7000 -Istubs/modem_rtos -o $@ $(P25_MEMORY)
vocoderMemoryTestNoP25: $(P25_MEMORY) $(P25_MEMORY_DEPS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -UCONFIG_P25 -DCONFIG_P25=0 -UCONFIG_IMBE -DCONFIG_IMBE=0 \
		-I$(ROOT)/platform/hrc7000 -Istubs/modem_rtos -o $@ $(P25_MEMORY)

P25_HRC7 := $(ROOT)/drivers/modem/hrc7000
P25_GLUE := modemGlueTest.c $(P25_HRC7)/hrc7000_air.c $(P25_HRC7)/hrc7000_dmr.c \
	$(P25_HRC7)/hrc7000_bs.c $(P25_HRC7)/hrc7000.c $(CORE)/dmr/fec/dmrFec.c $(CORE)/air/air_softmodem.c
P25_GLUE_DEPS := $(ROOT)/platform/hrc7000/dmr_glue.c $(wildcard $(ROOT)/platform/hrc7000/*.h) \
	$(wildcard $(P25_HRC7)/*.h stubs/modem_rtos/*.h) $(P25_HEADERS)
P25_GLUE_FLAGS := -I$(ROOT)/platform/hrc7000 -Istubs/modem_rtos
P25_PCM := pcmGateTest.c sim/afsk_platform_sim.c $(P25_HRC7)/hrc7000.c \
	$(CORE)/aprs/afsk.c $(CORE)/aprs/hdlc.c $(CORE)/aprs/ax25.c
P25_PCM_DEPS := $(ROOT)/platform/hrc7000/audio_pcm.c $(ROOT)/platform/hrc7000/hrc_afsk.c \
	$(P25_GLUE_DEPS) $(ROOT)/drivers/audio/miclevel/miclevel.h
P25_PCM_FLAGS := $(P25_FLAGS) $(P25_GLUE_FLAGS) -DCONFIG_APRS_RX=0

pcmGateTest: $(P25_PCM) $(P25_PCM_DEPS)
	$(CC) $(CFLAGS) $(P25_PCM_FLAGS) -DCONFIG_VOX=0 -o $@ $(P25_PCM) -lm
pcmGateTestVox: $(P25_PCM) $(P25_PCM_DEPS)
	$(CC) $(CFLAGS) $(P25_PCM_FLAGS) -DCONFIG_VOX=1 -o $@ $(P25_PCM) -lm
pcmGateTestNoAir: $(P25_PCM) $(P25_PCM_DEPS)
	$(CC) $(CFLAGS) $(P25_PCM_FLAGS) -UCONFIG_P25 -DCONFIG_P25=0 -UCONFIG_IMBE -DCONFIG_IMBE=0 \
		-DCONFIG_VOX=0 -o $@ $(P25_PCM) -lm
pcmGateTestNoAirVox: $(P25_PCM) $(P25_PCM_DEPS)
	$(CC) $(CFLAGS) $(P25_PCM_FLAGS) -UCONFIG_P25 -DCONFIG_P25=0 -UCONFIG_IMBE -DCONFIG_IMBE=0 \
		-DCONFIG_VOX=1 -o $@ $(P25_PCM) -lm
modemGluePcmTest: $(P25_GLUE) sim/pcm_platform_sim.c $(P25_PCM_DEPS)
	$(CC) $(CFLAGS) $(P25_FLAGS) $(P25_GLUE_FLAGS) -DCONFIG_HOTSPOT_SPLIT=0 -DCONFIG_VOX=0 \
		-DMODEM_TEST_PCM -o $@ $(P25_GLUE) sim/pcm_platform_sim.c -lm

modemGlueTest: $(P25_GLUE) $(P25_GLUE_DEPS)
	$(CC) $(CFLAGS) $(P25_FLAGS) $(P25_GLUE_FLAGS) -DCONFIG_HOTSPOT_SPLIT=0 -o $@ $(P25_GLUE) -lm
modemGlueTestSplit: $(P25_GLUE) $(P25_GLUE_DEPS)
	$(CC) $(CFLAGS) $(P25_FLAGS) $(P25_GLUE_FLAGS) -DCONFIG_HOTSPOT_SPLIT=1 -o $@ $(P25_GLUE) -lm
modemGlueTestNoAir: $(P25_GLUE) $(P25_GLUE_DEPS)
	$(CC) $(CFLAGS) $(P25_FLAGS) $(P25_GLUE_FLAGS) -UCONFIG_P25 -DCONFIG_P25=0 \
		-UCONFIG_IMBE -DCONFIG_IMBE=0 -DCONFIG_HOTSPOT_SPLIT=0 -o $@ $(P25_GLUE) -lm
modemGlueTestNoAirSplit: $(P25_GLUE) $(P25_GLUE_DEPS)
	$(CC) $(CFLAGS) $(P25_FLAGS) $(P25_GLUE_FLAGS) -UCONFIG_P25 -DCONFIG_P25=0 \
		-UCONFIG_IMBE -DCONFIG_IMBE=0 -DCONFIG_HOTSPOT_SPLIT=1 -o $@ $(P25_GLUE) -lm

hrc7000AirTest: hrc7000AirTest.c fuzz.h $(P25_HRC7)/hrc7000_air.c $(P25_HRC7)/hrc7000_air.h \
	$(P25_HRC7)/hrc7000_dmr.c $(P25_HRC7)/hrc7000.c $(CORE)/dmr/fec/dmrFec.c $(P25_HEADERS) \
	$(wildcard $(P25_HRC7)/*.h)
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ hrc7000AirTest.c $(P25_HRC7)/hrc7000_air.c \
		$(P25_HRC7)/hrc7000_dmr.c $(P25_HRC7)/hrc7000.c $(CORE)/dmr/fec/dmrFec.c

boardAirTest: boardAirTest.c $(CORE)/air/air_session.c $(P25_FRAMER) $(P25_HEADERS) $(ROOT)/hal/board.h
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ boardAirTest.c $(CORE)/air/air_session.c $(P25_FRAMER)

fecTest: fecTest.c $(P25_FEC) $(P25_HEADERS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ fecTest.c $(P25_FEC)

airFramerTest: airFramerTest.c $(P25_FRAMER) $(P25_HEADERS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ airFramerTest.c $(P25_FRAMER)

p25Test: p25Test.c $(P25_UNITS) $(P25_FRAMER) $(P25_FEC) $(P25_HEADERS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ p25Test.c $(P25_UNITS) $(P25_FRAMER) $(P25_FEC)

p25EngineTest: p25EngineTest.c $(P25_ENGINE) $(P25_HEADERS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ p25EngineTest.c $(P25_ENGINE)

p25EngineTestTrunk: p25EngineTest.c $(P25_ENGINE) $(CORE)/p25/p25_tsbk.c $(P25_HEADERS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -UCONFIG_P25_TRUNK -DCONFIG_P25_TRUNK=1 -o $@ \
		p25EngineTest.c $(P25_ENGINE) $(CORE)/p25/p25_tsbk.c

mbeTest: mbeTest.c $(P25_MBE) $(P25_HEADERS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ mbeTest.c $(P25_MBE) -lm

imbeEncTest: imbeEncTest.c $(P25_MBE) $(P25_HEADERS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ imbeEncTest.c $(P25_MBE) -lm

p25TrunkTest: p25TrunkTest.c $(P25_ENGINE) $(CORE)/p25/p25_tsbk.c $(CORE)/p25/p25_follow.c \
	$(CORE)/air/air_softmodem.c $(P25_HEADERS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -UCONFIG_P25_TRUNK -DCONFIG_P25_TRUNK=1 -DCONFIG_AIR=1 -o $@ \
		p25TrunkTest.c $(P25_ENGINE) $(CORE)/p25/p25_tsbk.c $(CORE)/p25/p25_follow.c \
		$(CORE)/air/air_softmodem.c -lm

p25VoiceTest: p25VoiceTest.c $(CORE)/p25/p25Voice.c $(P25_MBE) $(P25_HEADERS) $(ROOT)/hal/hal_audio.h
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ p25VoiceTest.c $(CORE)/p25/p25Voice.c $(P25_MBE) -lm

fxpTest: fxpTest.c $(CORE)/dsp/fxp.h
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ fxpTest.c

airSoftmodemTest: airSoftmodemTest.c $(CORE)/air/air_softmodem.c $(P25_UNITS) $(P25_FRAMER) $(P25_FEC) $(P25_HEADERS)
	$(CC) $(CFLAGS) $(P25_FLAGS) -o $@ airSoftmodemTest.c $(CORE)/air/air_softmodem.c \
		$(P25_UNITS) $(P25_FRAMER) $(P25_FEC) -lm

.PHONY: run-p25 run-p25-build
run: run-p25
run-p25: run-p25-build
run-p25-build:
	$(PYTHON) -B p25BuildTest.py --make "$(MAKE)" --cc "$(CC)"
run-p25: fecTest airFramerTest p25Test p25EngineTest p25EngineTestTrunk mbeTest imbeEncTest \
	p25TrunkTest p25VoiceTest fxpTest airSoftmodemTest boardAirTest hrc7000AirTest \
	modemGlueTest modemGlueTestNoAir modemGlueTestSplit modemGlueTestNoAirSplit
	./pcmGateTest
	./pcmGateTestNoAir
	./pcmGateTestVox
	./pcmGateTestNoAirVox
	./modemGluePcmTest
	./modemGlueTest
	./modemGlueTestNoAir
	./modemGlueTestSplit
	./modemGlueTestNoAirSplit
	./hrc7000AirTest
	./boardAirTest
	./fecTest
	./airFramerTest
	./p25Test
	./p25EngineTest
	./p25EngineTestTrunk
	./mbeTest
	./imbeEncTest
	./p25TrunkTest
	./p25VoiceTest
	./p25CodeplugTest
	./p25CodeplugTestTrunk
	./codeplugKvTest
	./p25StoredTest
	./p25StoredTestTrunk
	./codeplugTagTest
	./p25AdmissionTest
	./p25AdmissionTestTrunk
	./radioP25OwnerTest
	./radioP25OwnerTestTrunk
	./radioP25StateTest
	./radioP25TrunkStateTest
	./radioP25OffTest
	./codeplugTagTestNoP25
	./ambeVoiceTest
	./vocoderMemoryTest
	./vocoderMemoryTestTrunk
	./vocoderMemoryTestNoP25
	./fxpTest
	./airSoftmodemTest

run-p25: pcmGateTest pcmGateTestNoAir pcmGateTestVox pcmGateTestNoAirVox modemGluePcmTest ambeVoiceTest
run-p25: vocoderMemoryTest vocoderMemoryTestTrunk vocoderMemoryTestNoP25
run-p25: p25CodeplugTest p25CodeplugTestTrunk
run-p25: codeplugKvTest
run-p25: p25StoredTest p25StoredTestTrunk
run-p25: codeplugTagTest
run-p25: p25AdmissionTest p25AdmissionTestTrunk
run-p25: radioP25OwnerTest radioP25OwnerTestTrunk
run-p25: radioP25StateTest
run-p25: radioP25TrunkStateTest
run-p25: radioP25OffTest codeplugTagTestNoP25
