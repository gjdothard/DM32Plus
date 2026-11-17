// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Native dialog cancellation and Cancel buttons use the same discard decision.
export function guardDialog(dialog, changed, message) {
	const close = () => {
		if (!changed() || confirm(message)) dialog.close();
	};
	dialog.addEventListener("cancel", event => {
		event.preventDefault();
		close();
	});
	const leaving = event => {
		if (!changed()) return;
		event.preventDefault();
		event.returnValue = "";
	};
	window.addEventListener("beforeunload", leaving);
	dialog.addEventListener("close", () => window.removeEventListener("beforeunload", leaving), { once: true });
	return close;
}
