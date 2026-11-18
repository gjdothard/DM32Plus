# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Garrett Dothard
# Copyright (c) 2026 The RadioFoundry Authors
# P25 foundation imported from RadioFoundry c34dc440. Host-only until the board,
# radio core and codeplug are integrated; these flags do not enable firmware TX.

P25_FEC := $(wildcard $(CORE)/fec/*.c)
P25_UNITS := $(CORE)/p25/p25_unit.c $(CORE)/p25/p25_rx.c $(CORE)/p25/p25_call.c
P25_FRAMER := $(CORE)/air/air_framer.c
P25_ENGINE := $(CORE)/p25/p25_engine.c $(CORE)/air/air_session.c $(P25_FRAMER) $(P25_UNITS) $(P25_FEC)
P25_MBE := $(wildcard $(CORE)/mbe/*.c)
P25_FLAGS := -I$(ROOT)/hal -Wshadow -DCONFIG_P25=1 -DCONFIG_IMBE=1 -DCONFIG_P25_TRUNK=0
P25_HEADERS := $(wildcard $(CORE)/p25/*.h $(CORE)/air/*.h $(CORE)/fec/*.h $(CORE)/mbe/*.h) \
	$(CORE)/dsp/fxp.h $(ROOT)/hal/hal_air.h $(ROOT)/hal/hal_baseband.h

TESTS += fecTest airFramerTest p25Test p25EngineTest p25EngineTestTrunk mbeTest imbeEncTest \
	p25TrunkTest p25VoiceTest fxpTest airSoftmodemTest boardAirTest

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

.PHONY: run-p25
run: run-p25
run-p25: fecTest airFramerTest p25Test p25EngineTest p25EngineTestTrunk mbeTest imbeEncTest \
	p25TrunkTest p25VoiceTest fxpTest airSoftmodemTest boardAirTest
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
	./fxpTest
	./airSoftmodemTest
