<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# UI design consistency audit

Reviewed November 17, 2026, at revision `7bb7191`.

The six implementation families were approved after review of the proposed layouts. The table below records
the findings before implementation; each family is tracked by its separate implementation commit.

The next redesigns should cover Activity / Logs, the Radio submenu and all-channels list, Talk to and Find,
Dual Watch menus, and Settings subpages. These are common paths that switch from the card design to the
standard list presentation. Quick Controls keeps its current numbered order.

## Review method

Rendered and visually inspected 60 representative states in the host simulator at `240x320i10x5`, using
`branded-home` and the current Main Menu routes. Compared them with Home, Main Menu, caller Actions,
Quick Controls, and the card lists and editors. Inspected the screen definitions and drawing dispatch too.

The screenshots, contact sheets, route manifest, and rendering script are local audit artifacts under
`build/ui-audit/` and `build/ui-tools/audit_screens.py`. They are ignored build output.
This is a design backlog, not an implementation or hardware test record. Smaller displays, every translation,
and every feature configuration still need review when a screen changes.

The references are the [UI spec](../spec/ui.md), [design principles](principles.md),
[screen drawing dispatch](../../core/ui/ui_app.c), [shared drawing functions](../../core/ui/ui_screens.c),
and [screen definitions](../../core/ui/ui_menus.c).

## Redesign backlog

Priority 1 covers common navigation. Priority 2 completes the workflows beneath it. Priority 3 covers
specialized screens whose information hierarchy should stay specific to their task.

| Priority | Pages | Observed inconsistency | Proposed direction |
|---|---|---|---|
| 1 | Activity / Logs (`recent`) | The Main Menu opens a page titled Recent, with the old status strip and flat rows. | Use the shared card header and footer. Show caller, target, direction, and age in stable positions. Settle the relationship between the menu label and page title. |
| 1 | Radio submenu (`menu-section`, Radio) | Its two entries use the old strip and list between a card Main Menu and Quick Controls. | Use numbered navigation cards for Quick Controls and Channels. Preserve both shortcuts. |
| 1 | All Channels (`channels`) | Radio opens a flat list, while a zone's channels already use cards. | Reuse the channel-list visual treatment. Preserve global numbering, filtering, New from current, and the different OK and CONTEXT actions. |
| 1 | Find (`find`) | Typing on Home replaces the card frame with the old strip and an amber query line. | Put the query in a consistent search region. Distinguish channels, contacts, talkgroups, frequencies, and settings without changing result order or keypad behavior. |
| 1 | Talk target picker (`picker`) | Quick Controls opens an old-style Talk to list. | Reuse contact and talkgroup row styling. Keep IDs, Default/Listen state, temporary selection, and permanent selection explicit. |
| 1 | Dual Watch actions (`dual`) | The card Home's CONTEXT menu uses a flat numbered list. | Use the same numbered action cards as caller Actions. Retain the selected A/B pane, channel names, and stop destination. |
| 1 | Settings groups and numeric editors (`group`) | The Settings root uses cards; Display, Keys, and the other groups return to flat rows and old inline editors. | Apply one shared field-list frame to every group. Make focus, editing, current value, units, and allowed range clear. Preserve immediate-save behavior. |
| 2 | Dual Watch channel and zone pickers (`dual_pick`, `dual_zone`) | Both use old list frames, despite selecting channels shown as cards on Home. | Reuse channel and zone picker styling. Keep the pane letter and unavailable-channel explanation visible. |
| 2 | Channel actions, Zone options, Delete Zone (`zchannel`, `zoneedit`, `zonedel`) | The parent zone list is a card screen; its actions and delete confirmation are not. | Use numbered action cards and a shared confirmation layout. Preserve action order, warnings, and the safe default. |
| 2 | Add Channel, TX contact/RX group picker, In Zones (`zpick`, `chpick`, `chzones`) | These selection pages retain the old strip and flat rows. | Share picker components with the redesigned lists. Preserve filtering, None, membership state, and immediate membership writes. |
| 2 | Sharing recipients, received offer, preview, destination zone (`zone_share_to`, `zone_offer`, `zone_offer_preview`, `zone_offer_zone`) | Sharing switches to old list and text layouts. | Use a summary card for sender and offer details, common recipient/zone pickers, and clear eligibility and import feedback. Preserve Import/Preview/Reject semantics. |
| 2 | My Radio and Language (`dmr-id`, `language`) | Both are old-style pages reached from card Settings. | Use shared field and selection cards. Separate saved identity from editable fields; keep the active language and unavailable catalogs explicit. |
| 2 | Factory reset and crash-report prompt/recipient (`confirm`, `crash`, `crash_to`) | Confirmation and recovery pages use older text/list frames. | Bring them into the shared confirmation and picker family. Keep consequences, destination, and cancellation obvious; preserve existing default choices. |
| 2 | Tool information and Roaming (`info`, `roaming`) | Card Tools opens flat explanatory text, sparse status pages, or a flat numbered menu. | Use status summaries and action cards. Keep explanations readable and avoid adding empty decorative cards. Cover Hotspot, USB Network, Single-Freq Repeater, Battery/Supply, and roaming state. |
| 2 | APRS Position and Heard (`aprs-pos`, `aprs-heard`) | The APRS root uses cards, but these child pages do not. | Reuse field and activity-list components. Preserve coordinate entry, source selection, and age indicators. |
| 2 | Last position (`contact-position`) | Caller Actions opens an old frame with a compact distance/age summary. | Use a position summary with contact identity, distance, bearing, report age, and request status. Keep stale or missing information explicit. |
| 3 | Volume and frequency trim (`volume`, `freq-trim`) | Their gauges work, but their headers and footers use the old presentation. | Adapt the surrounding frame. Retain large values, scales, units, and the specialized controls. |
| 3 | Active Hotspot (`hotspot`) | Entering hotspot mode replaces the card Home with the old strip and plain status text. | Use a dedicated status layout with a clear mode, link/call state, frequency, and exit action. |
| 3 | Diagnostics (`diag`) | A dense diagnostic text page has its own heading and footer. | Keep the dense data useful. Consider a shared frame only if it does not obscure readings or reduce their practical value. |
| 3 | Emergency (`emergency`) | The urgent red page has an older header/footer. | Harmonize the surrounding controls while retaining its unmistakable emergency treatment and acknowledgement behavior. |
| 3 | About (`about`) | It uses the older text-page layout. | Align title and footer treatment while preserving readable, scrollable credits and the deliberate absence of the idle status strip. |

The legacy sharing screen (`zone_share`) and message-history conversation options (`conversation`) also use
the generic list path. They were identified in source rather than included in the 60 rendered states;
review them when working on their corresponding screen families.

## Existing design to reuse

The reviewed Home, Main Menu, caller Actions, Quick Controls, Zones, zone channels, contact editor, name entry,
Unsaved Changes, Messages, composer, GPS status, APRS root, Scan, and operating-talkgroup actions already use
the newer presentation. These are reference components, not candidates for another wholesale redesign.

Other variants still need checks during implementation: GPS tabs, live/history message threads, contact tabs,
scan tabs, text-entry modes, and the channel editor's full set of fields. Existing card routing for these
screens does not replace visual review of each state.

RX/TX on the reviewed card Home and incoming message/call-alert banners already retain the card context.
Boot and loader screens serve different purposes and do not need to become menu cards.

## Future improvements

| Item | Evidence or reason | Suggested follow-up |
|---|---|---|
| Explain read-only controls accurately | Pressing the FM memory Offset tile displays Not in this build, although the tile correctly shows the stored offset. | Explain that the value is stored in the channel and direct the user to channel editing or the VFO TX frequency control. Keep the tile order. |
| Standardize footer placement | Numbered menus put Back at the right edge. Several card field lists and editors include it in a left-aligned hint string. | Use one footer layout that preserves complete key/action groups and places navigation consistently. Retain text-entry deletion semantics. |
| Define focus and editing styles | Navigation cards use an outline; field and selection cards often use a solid cyan fill. | Document which visual state means focus, selected value, or active editing, then apply that rule by screen role. Avoid changing every highlight indiscriminately. |
| Avoid smaller text for a single long menu label | Tools renders Single-freq repeater in a smaller font than neighboring entries. | Review short translated labels, reserved columns, or wrapping before per-row font reduction. Apply the saved-contact menu's readability lesson consistently. |
| Reconcile Home call-state styling with the spec | The reviewed single-card RX/TX states change the outline to green/red. The spec says the selected outline remains cyan during calls. | Decide which behavior is intended, align the spec, and add a focused visual regression check. |
| Improve disabled/status explanations | Caller Actions dims both In contacts and Speak status, although one is a saved state and the other is unavailable functionality. | Keep state labels distinct from unavailable actions and provide an actionable explanation when selected. |
| Keep title and action wording consistent | The Main Menu says Activity / Logs, its page says Recent, and some editor headings use sentence case despite the title-case rule. | Review titles, row labels, hints, and translations together. Keep established key paths and user data unchanged. |
| Make presentation explicit in screen definitions | Card routing is a series of screen-specific branches in `list_draw`; new child screens can silently use the default list. | Consider small presentation metadata for navigation, fields, pickers, and confirmations, with shared renderers. Keep specialized gauges and urgent states separate. |
| Update gallery routes for the current design | Only selected gallery entries receive branded routes. Others retain old Main Menu paths or old Quick key numbers. | Validate every gallery route's final screen, give the current card design complete coverage, and regenerate only intended PNG changes. |
| Assert literal screen identifiers reliably | The simulator's expectation parser turns underscores into spaces, including in screen names such as `dual_pick` and `zone_offer`. | Preserve literal identifiers for screen checks, retain space decoding for displayed text, and add parser regression tests. |
| Test visual consistency as well as geometry | The existing layout audit can pass while adjacent pages use different headers, footers, and list treatments. | Add checks for the intended presentation family and explicit entry flows for each migrated screen. Cover long translations, empty lists, unavailable features, RX/TX, and alternate themes. |

## Suggested implementation sequence

1. Activity / Logs, then Radio and All Channels.
2. Talk to and Find, followed by their related selection lists.
3. Dual Watch actions and its channel/zone pickers.
4. Settings groups, numeric editors, My Radio, and Language.
5. Zone/channel actions, confirmations, and sharing workflows.
6. Tool summaries, Roaming, APRS child pages, and contact position.
7. Specialized gauges, operating modes, recovery pages, and shared visual polish.

For each change, preserve existing shortcuts, Quick Controls order, PTT, Back, long-press behavior, and save
semantics. Add or update a host flow that opens the page and verifies its screen, review its gallery image,
check the smaller display classes and feature switches involved, and update translations and documentation
when wording or behavior changes.
