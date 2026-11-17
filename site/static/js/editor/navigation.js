// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Shared keyboard behavior for the editor's main and record-section tabs.
export function nextTab(key, index, count) {
	if (!count) return null;
	switch (key) {
	case "ArrowRight": return (index + 1) % count;
	case "ArrowLeft": return (index + count - 1) % count;
	case "Home": return 0;
	case "End": return count - 1;
	default: return null;
	}
}

export function keyboardTabs(list, activate) {
	list.addEventListener("keydown", event => {
		const current = event.target.closest('[role="tab"]');
		if (!current || current.parentElement !== list) return;
		const tabs = [...list.querySelectorAll('[role="tab"]')].filter(tab => !tab.hidden && !tab.disabled);
		const index = nextTab(event.key, tabs.indexOf(current), tabs.length);
		if (index === null) return;
		event.preventDefault();
		activate(tabs[index]);
		tabs[index].focus();
	});
}
