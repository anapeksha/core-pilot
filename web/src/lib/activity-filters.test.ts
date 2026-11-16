import { describe, expect, it } from "vitest";
import {
  activityFilterParams,
  MAX_ACTIVITY_EVENT_TYPE_BYTES,
} from "./activity";

describe("activity URL filters", () => {
  it("defaults absent and unsupported filters without accepting readiness claims", () => {
    expect(activityFilterParams({})).toEqual({
      eventType: "all",
      severity: "all",
      category: "all",
    });
    expect(
      activityFilterParams({ severity: "ready", category: "matter_ready" }),
    ).toEqual({ eventType: "all", severity: "all", category: "all" });
  });

  it("retains valid deep-linked event types independent of loaded pages", () => {
    expect(
      activityFilterParams({
        eventType: "future_firmware_event",
        severity: "warning",
        category: "network",
      }),
    ).toEqual({
      eventType: "future_firmware_event",
      severity: "warning",
      category: "network",
    });
  });

  it("validates every supported severity and category", () => {
    for (const severity of ["info", "warning", "error"]) {
      expect(activityFilterParams({ severity }).severity).toBe(severity);
    }
    for (const category of [
      "automation",
      "ai",
      "network",
      "ota",
      "configuration",
      "other",
    ]) {
      expect(activityFilterParams({ category }).category).toBe(category);
    }
  });

  it("bounds event types by UTF-8 bytes and rejects ambiguous or control input", () => {
    expect(
      activityFilterParams({
        eventType: "a".repeat(MAX_ACTIVITY_EVENT_TYPE_BYTES),
      }).eventType,
    ).toHaveLength(MAX_ACTIVITY_EVENT_TYPE_BYTES);
    expect(
      activityFilterParams({
        eventType: "é".repeat(MAX_ACTIVITY_EVENT_TYPE_BYTES / 2),
      }).eventType,
    ).not.toBe("all");
    for (const eventType of [
      "",
      "a".repeat(MAX_ACTIVITY_EVENT_TYPE_BYTES + 1),
      "é".repeat(MAX_ACTIVITY_EVENT_TYPE_BYTES / 2 + 1),
      "bad\nkind",
      ["wifi_joined", "ota_activated"],
    ]) {
      expect(activityFilterParams({ eventType }).eventType).toBe("all");
    }
  });
});
