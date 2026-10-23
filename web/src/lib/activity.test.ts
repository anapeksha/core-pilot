import { describe, expect, it } from "vitest";
import type { ActivityEvent, ActivityPage } from "~/lib/api";
import {
  ACTIVITY_PAGE_LIMIT,
  MAX_ACTIVITY_ITEMS,
  activityCategory,
  activityEventTypeLabel,
  activitySeverity,
  appendActivityPage,
  flattenActivityPages,
} from "./activity";

const event = (
  sequence: number,
  kind: string,
  detail: string,
): ActivityEvent => ({
  sequence,
  unix_seconds: null,
  uptime_seconds: sequence,
  kind,
  detail,
});

const page = (events: ActivityEvent[]): ActivityPage => ({
  events,
  next_before_sequence: null,
  retained: events.length,
  dropped_events: 0,
  detail: "Events loaded.",
});

describe("activity helpers", () => {
  it("uses firmware-compatible bounded page sizes", () => {
    expect(ACTIVITY_PAGE_LIMIT).toBeLessThanOrEqual(32);
    expect(ACTIVITY_PAGE_LIMIT).toBeGreaterThan(0);
  });

  it("deduplicates, orders, and bounds accumulated cursor pages", () => {
    const current = [event(10, "automation_queued", "Queued.")];
    const accumulated = appendActivityPage(
      current,
      page([
        event(10, "automation_queued", "Duplicate."),
        event(9, "configuration_saved", "Saved."),
      ]),
    );
    expect(accumulated.map((item) => item.sequence)).toEqual([10, 9]);

    const many = Array.from({ length: MAX_ACTIVITY_ITEMS + 1 }, (_, index) =>
      event(index, "wifi_joined", "Connected."),
    );
    const bounded = appendActivityPage([], page(many));
    expect(bounded).toHaveLength(MAX_ACTIVITY_ITEMS);
    expect(bounded.at(-1)?.sequence).toBe(1);
  });

  it("flattens infinite-query pages without duplicates", () => {
    const flattened = flattenActivityPages([
      page([event(3, "wifi_joined", "Connected.")]),
      page([
        event(3, "wifi_joined", "Duplicate."),
        event(2, "time_sync_succeeded", "Synced."),
      ]),
    ]);

    expect(flattened.map((item) => item.sequence)).toEqual([3, 2]);
  });

  it("derives filters only from the firmware kind and detail fields", () => {
    expect(
      activitySeverity(event(1, "automation_execution_failed", "Failed.")),
    ).toBe("error");
    expect(activitySeverity(event(1, "wifi_joined", "Connected."))).toBe(
      "info",
    );
    expect(activityCategory(event(1, "automation_queued", "Queued."))).toBe(
      "automation",
    );
    expect(activityCategory(event(1, "configuration_saved", "Saved."))).toBe(
      "configuration",
    );
    expect(activityCategory(event(1, "ai_tool_completed", "Completed."))).toBe(
      "ai",
    );
    expect(activityCategory(event(1, "wifi_joined", "Connected."))).toBe(
      "network",
    );
    expect(activityCategory(event(1, "ota_activated", "Activated."))).toBe(
      "ota",
    );
    expect(activityCategory(event(1, "unknown", "No extra data."))).toBe(
      "other",
    );
    expect(activityEventTypeLabel("time_sync_failed")).toBe("time sync failed");
  });
});
