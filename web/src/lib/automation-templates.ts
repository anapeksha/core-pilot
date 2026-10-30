import {
  MAX_AUTOMATION_ACTIONS,
  type ActionDraft,
  type TriggerDraft,
} from "./automations";

export type AutomationTemplateId =
  | "turn-everything-off-at-night"
  | "weekday-morning-scene"
  | "motion-lighting"
  | "device-left-on"
  | "offline-notification"
  | "sunset-sunrise"
  | "arrival-departure";

export type AutomationTemplateDraft = {
  name: string;
  enabled: boolean;
  trigger: TriggerDraft;
  actions: ActionDraft[];
  selectedWeekdays: string[];
};

type AvailableAutomationTemplate = {
  id: AutomationTemplateId;
  title: string;
  description: string;
  available: true;
};

type UnavailableAutomationTemplate = {
  id: AutomationTemplateId;
  title: string;
  description: string;
  available: false;
  prerequisite: string;
};

export type AutomationTemplate =
  AvailableAutomationTemplate | UnavailableAutomationTemplate;

export const automationTemplates: readonly AutomationTemplate[] = [
  {
    id: "turn-everything-off-at-night",
    title: "Turn everything off at night",
    description: "Turn selected devices off every night at 11:00 PM.",
    available: false,
    prerequisite: "Requires the device-side scheduled automation runner.",
  },
  {
    id: "weekday-morning-scene",
    title: "Weekday morning scene",
    description: "Activate a selected scene on weekdays at 7:00 AM.",
    available: false,
    prerequisite:
      "Requires scheduled triggers and device-side scene execution.",
  },
  {
    id: "motion-lighting",
    title: "Motion lighting",
    description: "Turn lights on when motion is detected.",
    available: false,
    prerequisite: "Requires motion-event triggers from supported sensors.",
  },
  {
    id: "device-left-on",
    title: "Device left on",
    description: "Act when a device has remained on too long.",
    available: false,
    prerequisite: "Requires device-state duration triggers.",
  },
  {
    id: "offline-notification",
    title: "Offline notification",
    description: "Notify locally when a device goes offline.",
    available: false,
    prerequisite: "Requires device connectivity-change triggers.",
  },
  {
    id: "sunset-sunrise",
    title: "Sunset or sunrise",
    description: "Run actions relative to local sunrise or sunset.",
    available: false,
    prerequisite: "Requires a configured location and solar-time triggers.",
  },
  {
    id: "arrival-departure",
    title: "Arrival or departure",
    description: "Run actions when household members arrive or leave.",
    available: false,
    prerequisite: "Requires an opt-in presence or geofence trigger source.",
  },
];

export function applyAutomationTemplate(
  id: AutomationTemplateId,
  timezoneOffsetMinutes = 0,
): AutomationTemplateDraft | null {
  const template = automationTemplates.find((candidate) => candidate.id === id);
  if (!template?.available) return null;

  const draft = createAvailableDraft(id, timezoneOffsetMinutes);
  if (draft.actions.length > MAX_AUTOMATION_ACTIONS) return null;
  return draft;
}

function createAvailableDraft(
  id: AutomationTemplateId,
  timezoneOffsetMinutes: number,
): AutomationTemplateDraft {
  switch (id) {
    case "turn-everything-off-at-night":
      return {
        name: "Turn everything off at night",
        enabled: true,
        trigger: {
          type: "weekly",
          weekdaysMask: 127,
          time: "23:00",
          timezoneOffsetMinutes,
        },
        actions: [{ type: "set_on_off", deviceId: "", on: false }],
        selectedWeekdays: ["1", "2", "4", "8", "16", "32", "64"],
      };
    case "weekday-morning-scene":
      return {
        name: "Weekday morning scene",
        enabled: true,
        trigger: {
          type: "weekly",
          weekdaysMask: 31,
          time: "07:00",
          timezoneOffsetMinutes,
        },
        actions: [{ type: "activate_scene", sceneId: "" }],
        selectedWeekdays: ["1", "2", "4", "8", "16"],
      };
    default:
      throw new Error(`Template ${id} is not available`);
  }
}
