# Dashboard interaction contracts

The dashboard stays useful when requests are slow or fail. These behaviors are covered by browser tests against simulated firmware responses; they are not evidence of hardware or Matter readiness.

## Loading and refreshes

Home, Devices, Rooms, Groups, Scenes, and Automations distinguish initial loading from refreshes:

- Initial loading uses visible, non-interactive placeholders and a spoken loading label.
- An initial failure shows an error and Retry, not an empty inventory or a healthy fallback.
- A failed refresh retains previously loaded content and labels it as potentially out of date.
- Retry preserves keyboard focus and does not replace a working card with a skeleton.
- Dependency-sensitive actions remain blocked when their required inventory cannot be verified, including confirmations that were already open.

Activity also keeps its loaded rows when an older page fails. Automatic pagination stops after the failure; **Retry older** is explicit. Appending rows does not reset the viewport. Changing a filter does.

## Shareable filters

Filters live inside the hash-router URL. Unknown query parameters are preserved. Defaults are omitted and invalid values are normalized without adding a history entry.

| Page | Parameters |
| --- | --- |
| Activity | `eventType`, `severity`, `category` |
| Devices | `q`, `protocol`, `availability`, `density` |

Examples:

```text
/#/activity?severity=warning&category=network
/#/devices?q=lamp&protocol=matter&availability=online&density=compact
```

Activity counts describe retained events, loaded events, and matches within the loaded events. Filtering does not imply that unloaded history has been searched.

## Editing saved models

Rooms, Groups, Scenes, and Automations protect unsaved create/edit sheets. The guard compares current values with an in-memory snapshot taken after initialization. Opening an editor or reverting its changes does not trigger a warning.

Closing a dirty editor or leaving its route offers **Keep editing** and **Discard changes**. Keep editing preserves the draft and focus. An accepted save closes normally; a failed save leaves the draft available. Reload/tab-close protection uses the browser's own warning. Drafts are not persisted or logged.

This guard is intentionally limited to model editors; it does not serialize credentials or make claims about unrelated System or assistant configuration forms.

## Commands, shortcuts, and accessibility

- Each device has a visible **Actions** menu usable by pointer, touch, and keyboard. Right-click remains available.
- Accepting a Matter request is not the same as completing it. Power displays only reported device state.
- Automation completion is shown only after the existing operation-status request reaches a terminal result.
- The command palette displays `⌘K` on Apple platforms and `Ctrl+K` elsewhere, with matching accessible shortcut metadata.
- The assistant transcript is a stable polite live log. Completed replies are added to that log, rather than depending on a temporary thinking indicator.
- Virtual Activity rows expose positions within the loaded, filtered set. Scroll regions are keyboard-focusable.

Run the commands in `CONTRIBUTING.md` before changing these contracts. Playwright includes axe checks without rule exclusions, pointer/disabled/drag cursor checks, and keyboard focus tests. Automated checks cannot establish screen-reader interoperability: manually verify VoiceOver announcements, virtual-list browsing, focus return, zoom, and touch behavior before making broader accessibility claims.
