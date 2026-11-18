// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Compile-only size markers for the target ABI. Never link this object into firmware.
#include "config.h"

#if CONFIG_P25
#include "air/air_session.h"
#include "air/air_softmodem.h"
#include "p25/p25_engine.h"
#include "p25/p25Voice.h"
#include "p25/p25_workspace.h"
#if CONFIG_P25_TRUNK
#include "p25/p25_follow.h"
#endif

unsigned char p25_bytes_engine[sizeof(p25_engine_t)];
unsigned char p25_bytes_voice[sizeof(p25_voice_mem_t)];
unsigned char p25_bytes_workspace[sizeof(p25_workspace_t)];
unsigned char p25_bytes_workspace_lease[sizeof(p25_workspace_lease_t)];
unsigned char p25_bytes_session[sizeof(air_session_t)];
unsigned char p25_bytes_softmodem[sizeof(air_softmodem_t)];
#if CONFIG_P25_TRUNK
unsigned char p25_bytes_follower[sizeof(p25_follow_t)];
#endif
#endif
