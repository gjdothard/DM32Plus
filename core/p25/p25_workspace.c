// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
#include "p25_workspace.h"
#if CONFIG_P25
#include <string.h>

bool p25_workspace_acquire(p25_workspace_lease_t *lease, const hal_vocoder_t *vocoder)
{
	if (lease == NULL || lease->vocoder != NULL || lease->loan.data != NULL
	    || lease->loan.bytes != 0 || lease->loan.token != 0 || vocoder == NULL
	    || vocoder->lend == NULL || vocoder->reclaim == NULL)
		return false;
	hal_vocoder_loan_t loan = {0};
	if (!vocoder->lend(sizeof(p25_workspace_t), _Alignof(p25_workspace_t), &loan))
		return false;
	lease->loan = loan;
	lease->vocoder = vocoder;
	if (loan.data == NULL || loan.bytes < sizeof(p25_workspace_t) || loan.token == 0
	    || (uintptr_t)loan.data % _Alignof(p25_workspace_t) != 0) {
		// A broken provider must not make its outstanding loan unreachable if cleanup fails.
		p25_workspace_release(lease);
		return false;
	}
	memset(loan.data, 0, sizeof(p25_workspace_t));
	return true;
}

bool p25_workspace_release(p25_workspace_lease_t *lease)
{
	if (lease == NULL || lease->vocoder == NULL || lease->vocoder->reclaim == NULL)
		return false;
	if (!lease->vocoder->reclaim(&lease->loan))
		return false;
	*lease = (p25_workspace_lease_t){0};
	return true;
}
#endif
