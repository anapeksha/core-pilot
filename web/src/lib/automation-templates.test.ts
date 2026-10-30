import { describe, expect, it } from "vitest";

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

  it("does not apply unavailable templates", () => {
    const unavailableIds = automationTemplates
      .filter((template) => !template.available)
      .map((template) => template.id);

    for (const id of unavailableIds) {
      expect(applyAutomationTemplate(id as AutomationTemplateId)).toBeNull();
    }
  });
});
