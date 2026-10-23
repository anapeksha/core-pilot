import { describe, expect, it } from "vitest";
import {
  actionSummary,
  automationHasLegacyValues,
  decodeAction,
  decodeTrigger,
  encodeAction,
  encodeTrigger,
  triggerSummary,
  validateAutomationDraft,
  type ActionDraft,
} from "./automations";

const action: ActionDraft = {
  type: "local_notification",
  message: "Ready",
};

describe("automation wire contracts", () => {
  it("keeps weekly weekday bits Monday-first through encoding", () => {
    const weekdays = [
      ["Monday", 1],
      ["Tuesday", 2],
      ["Wednesday", 4],
      ["Thursday", 8],
      ["Friday", 16],
      ["Saturday", 32],
      ["Sunday", 64],
    ] as const;

    for (const [, weekdaysMask] of weekdays) {
      expect(
        encodeTrigger({
          type: "weekly",
          weekdaysMask,
          time: "07:30",
          timezoneOffsetMinutes: 0,
        }),
      ).toBe(`weekly|${weekdaysMask}|07:30|0`);
    }
  });

  it("rejects impossible dates, invalid times, and unsupported offsets", () => {
    const validateOnce = (date: string, time = "07:30", offset = 0) =>
      validateAutomationDraft(
        "Morning",
        {
          type: "once",
          date,
          time,
          timezoneOffsetMinutes: offset,
        },
        [action],
      );

    expect(validateOnce("2024-02-29")).toBeNull();
    expect(validateOnce("2025-02-29")).toMatch(/valid date/);
    expect(validateOnce("2024-04-31")).toMatch(/valid date/);
    expect(validateOnce("2024-01-01", "24:00")).toMatch(/valid date/);
    expect(validateOnce("2024-01-01", "07:30", -721)).toMatch(/UTC offset/);
    expect(validateOnce("2024-01-01", "07:30", 841)).toMatch(/UTC offset/);
  });

  it("accepts all bounded Monday-first weekly masks", () => {
    for (const weekdaysMask of [1, 2, 4, 8, 16, 32, 64, 127]) {
      expect(
        validateAutomationDraft(
          "Weekly",
          {
            type: "weekly",
            weekdaysMask,
            time: "23:59",
            timezoneOffsetMinutes: 840,
          },
          [action],
        ),
      ).toBeNull();
    }
  });

  it("round-trips supported triggers and actions", () => {
    const triggers = [
      { type: "manual" as const },
      {
        type: "once" as const,
        date: "2026-09-28",
        time: "07:30",
        timezoneOffsetMinutes: 330,
      },
      {
        type: "weekly" as const,
        weekdaysMask: 65,
        time: "09:15",
        timezoneOffsetMinutes: -240,
      },
    ];
    for (const trigger of triggers) {
      expect(decodeTrigger(encodeTrigger(trigger))).toEqual(trigger);
      expect(triggerSummary(trigger)).toBeTruthy();
    }

    const actions: ActionDraft[] = [
      { type: "set_on_off", deviceId: "light-1", on: true },
      { type: "set_on_off", deviceId: "light-1", on: false },
      { type: "set_level", deviceId: "light-1", level: 42 },
      { type: "activate_scene", sceneId: "evening" },
      { type: "local_notification", message: "Ready" },
      { type: "delay", milliseconds: 250 },
    ];
    for (const value of actions) {
      expect(decodeAction(encodeAction(value))).toEqual(value);
      expect(actionSummary(value)).toBeTruthy();
    }
  });

  it("treats removed and malformed records as legacy instead of executable", () => {
    for (const value of ["mqtt|home/events|equals|on", "once|bad", "unknown"]) {
      expect(decodeTrigger(value)).toEqual({ type: "legacy", value });
      expect(triggerSummary({ type: "legacy", value })).toContain("Legacy");
    }
    for (const value of ["publish|home/light|on|0", "level|light", "unknown"]) {
      expect(decodeAction(value)).toEqual({ type: "legacy", value });
      expect(actionSummary({ type: "legacy", value })).toContain("Legacy");
    }
  });

  it("rejects invalid names, trigger bounds, and action bounds", () => {
    const manual = { type: "manual" as const };
    expect(validateAutomationDraft("", manual, [action])).toMatch(/name/);
    expect(validateAutomationDraft("x".repeat(65), manual, [action])).toMatch(
      /name/,
    );
    expect(validateAutomationDraft("Valid", manual, [])).toMatch(
      /one and eight/,
    );
    expect(
      validateAutomationDraft("Valid", manual, Array(9).fill(action)),
    ).toMatch(/one and eight/);
    expect(
      validateAutomationDraft("Valid", { type: "legacy", value: "old" }, [
        action,
      ]),
    ).toMatch(/supported trigger/);

    const invalidActions: Array<[ActionDraft, RegExp]> = [
      [{ type: "set_on_off", deviceId: "", on: true }, /device/],
      [{ type: "set_level", deviceId: "light", level: 101 }, /0 to 100/],
      [{ type: "activate_scene", sceneId: "" }, /scene/],
      [{ type: "local_notification", message: "" }, /1–72/],
      [{ type: "local_notification", message: "bad|message" }, /1–72/],
      [{ type: "delay", milliseconds: 60_001 }, /60,000/],
      [{ type: "legacy", value: "old" }, /legacy action/],
    ];
    for (const [invalidAction, message] of invalidActions) {
      expect(validateAutomationDraft("Valid", manual, [invalidAction])).toMatch(
        message,
      );
    }
  });

  it("detects legacy persisted values", () => {
    expect(
      automationHasLegacyValues({
        id: "one",
        name: "One",
        enabled: true,
        trigger: "manual",
        actions: ["notify|Ready"],
      }),
    ).toBe(false);
    expect(
      automationHasLegacyValues({
        id: "old",
        name: "Old",
        enabled: false,
        trigger: "mqtt|events|equals|on",
        actions: ["notify|Ready"],
      }),
    ).toBe(true);
  });
});
