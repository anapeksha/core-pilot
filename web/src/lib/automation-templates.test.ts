import { describe, expect, it } from "vitest";
import { MAX_AUTOMATION_ACTIONS } from "./automations";
import {
  applyAutomationTemplate,
  automationTemplates,
  type AutomationTemplateId,
} from "./automation-templates";

describe("automation template catalog", () => {
  it("includes the requested available and unavailable templates", () => {
    expect(
      automationTemplates.map(({ id, available }) => ({ id, available })),
    ).toEqual([
      { id: "turn-everything-off-at-night", available: true },
      { id: "weekday-morning-scene", available: true },
      { id: "motion-lighting", available: false },
      { id: "device-left-on", available: false },
      { id: "offline-notification", available: false },
      { id: "sunset-sunrise", available: false },
      { id: "arrival-departure", available: false },
    ]);

    for (const template of automationTemplates) {
      if (!template.available) expect(template.prerequisite).toBeTruthy();
    }
  });

  it("applies the nightly template with unresolved device selection", () => {
    expect(
      applyAutomationTemplate("turn-everything-off-at-night", 330),
    ).toEqual({
      name: "Turn everything off at night",
      enabled: true,
      trigger: {
        type: "weekly",
        weekdaysMask: 127,
        time: "23:00",
        timezoneOffsetMinutes: 330,
      },
      actions: [{ type: "set_on_off", deviceId: "", on: false }],
      selectedWeekdays: ["1", "2", "4", "8", "16", "32", "64"],
    });
  });

  it("applies the weekday template with unresolved scene selection", () => {
    expect(applyAutomationTemplate("weekday-morning-scene", -240)).toEqual({
      name: "Weekday morning scene",
      enabled: true,
      trigger: {
        type: "weekly",
        weekdaysMask: 31,
        time: "07:00",
        timezoneOffsetMinutes: -240,
      },
      actions: [{ type: "activate_scene", sceneId: "" }],
      selectedWeekdays: ["1", "2", "4", "8", "16"],
    });
  });

  it("never invents selection IDs or exceeds the action bound", () => {
    for (const template of automationTemplates) {
      const draft = applyAutomationTemplate(template.id);
      if (!template.available) {
        expect(draft).toBeNull();
        continue;
      }

      expect(draft).not.toBeNull();
      expect(draft!.actions.length).toBeLessThanOrEqual(MAX_AUTOMATION_ACTIONS);
      for (const action of draft!.actions) {
        if (action.type === "set_on_off" || action.type === "set_level") {
          expect(action.deviceId).toBe("");
        }
        if (action.type === "activate_scene") expect(action.sceneId).toBe("");
      }
    }
  });

  it("returns independent editable drafts", () => {
    const first = applyAutomationTemplate("weekday-morning-scene")!;
    first.actions.push({ type: "delay", milliseconds: 100 });
    first.selectedWeekdays.pop();

    const second = applyAutomationTemplate("weekday-morning-scene")!;
    expect(second.actions).toEqual([{ type: "activate_scene", sceneId: "" }]);
    expect(second.selectedWeekdays).toEqual(["1", "2", "4", "8", "16"]);
  });

  it("does not apply unavailable templates", () => {
    const unavailableIds = automationTemplates
      .filter((template) => !template.available)
      .map((template) => template.id);

    for (const id of unavailableIds) {
      expect(applyAutomationTemplate(id as AutomationTemplateId)).toBeNull();
    }
  });
});
