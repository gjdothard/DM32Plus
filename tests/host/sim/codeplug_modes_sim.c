// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Existing radio fixtures supply decoded channel records rather than a flash image.
#include "codeplug/codeplug.h"
bool codeplug_radio_channel(uint16_t number, channel_t *out) { return codeplug_channel(number, out); }
