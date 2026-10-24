import type { Automation } from "./api";
import { entityLimits } from "./request-contracts";

export const MAX_AUTOMATION_ACTIONS = entityLimits.actions;
export const MAX_NOTIFICATION_BYTES = entityLimits.notification;
export const MAX_DELAY_MS = 60_000;
export const MIN_TIMEZONE_OFFSET_MINUTES = -720;
export const MAX_TIMEZONE_OFFSET_MINUTES = 840;

export type TriggerDraft =
  | { type: "manual" }
  | {
      type: "once";
      date: string;
      time: string;
      timezoneOffsetMinutes: number;
    }
  | {
      type: "weekly";
      weekdaysMask: number;
      time: string;
      timezoneOffsetMinutes: number;
    }
  | { type: "legacy"; value: string };

export type ActionDraft =
  | { type: "set_on_off"; deviceId: string; on: boolean }
  | { type: "set_level"; deviceId: string; level: number }
  | { type: "activate_scene"; sceneId: string }
  | { type: "local_notification"; message: string }
  | { type: "delay"; milliseconds: number }
  | { type: "legacy"; value: string };

const encode = (value: string) => value.trim();
const decode = (value: string) => value;

export function encodeTrigger(trigger: TriggerDraft): string {
  switch (trigger.type) {
    case "manual":
      return "manual";
    case "once":
      return `once|${trigger.date}|${trigger.time}|${trigger.timezoneOffsetMinutes}`;
    case "weekly":
      return `weekly|${trigger.weekdaysMask}|${trigger.time}|${trigger.timezoneOffsetMinutes}`;
    case "legacy":
      return trigger.value;
  }
}

export function decodeTrigger(value: string): TriggerDraft {
  const parts = value.split("|");
  if (value === "manual") return { type: "manual" };
  if (parts[0] === "once" && parts.length === 4) {
    return {
      type: "once",
      date: parts[1],
      time: parts[2],
      timezoneOffsetMinutes: Number(parts[3]) || 0,
    };
  }
  if (parts[0] === "weekly" && parts.length === 4) {
    return {
      type: "weekly",
      weekdaysMask: Number(parts[1]) || 0,
      time: parts[2],
      timezoneOffsetMinutes: Number(parts[3]) || 0,
    };
  }
  return { type: "legacy", value };
}

export function encodeAction(action: ActionDraft): string {
  switch (action.type) {
    case "set_on_off":
      return `on|${encode(action.deviceId)}|${action.on ? "1" : "0"}`;
    case "set_level":
      return `level|${encode(action.deviceId)}|${Math.round(action.level)}`;
    case "activate_scene":
      return `scene|${encode(action.sceneId)}`;
    case "local_notification":
      return `notify|${encode(action.message)}`;
    case "delay":
      return `delay|${Math.round(action.milliseconds)}`;
    case "legacy":
      return action.value;
  }
}

export function decodeAction(value: string): ActionDraft {
  const parts = value.split("|");
  if (parts[0] === "on" && parts.length === 3) {
    return {
      type: "set_on_off",
      deviceId: decode(parts[1]),
      on: parts[2] === "1",
    };
  }
  if (parts[0] === "level" && parts.length === 3) {
    return {
      type: "set_level",
      deviceId: decode(parts[1]),
      level: Number(parts[2]) || 0,
    };
  }
  if (parts[0] === "scene" && parts.length === 2) {
    return { type: "activate_scene", sceneId: decode(parts[1]) };
  }
  if (parts[0] === "notify" && parts.length === 2) {
    return { type: "local_notification", message: decode(parts[1]) };
  }
  if (parts[0] === "delay" && parts.length === 2) {
    return { type: "delay", milliseconds: Number(parts[1]) || 0 };
  }
  return { type: "legacy", value };
}

export function triggerSummary(trigger: TriggerDraft): string {
  switch (trigger.type) {
    case "manual":
      return "Manual start";
    case "once":
      return `Once on ${trigger.date || "an unset date"} at ${trigger.time}`;
    case "weekly":
      return `Weekly on selected days at ${trigger.time}`;
    case "legacy":
      return `Legacy trigger: ${trigger.value}`;
  }
}

export function actionSummary(action: ActionDraft): string {
  switch (action.type) {
    case "set_on_off":
      return `${action.on ? "Turn on" : "Turn off"} ${action.deviceId || "a device"}`;
    case "set_level":
      return `Set ${action.deviceId || "a device"} to ${action.level}%`;
    case "activate_scene":
      return `Activate scene ${action.sceneId || "not selected"}`;
    case "local_notification":
      return `Notify: ${action.message || "No message"}`;
    case "delay":
      return `Wait ${action.milliseconds} ms`;
    case "legacy":
      return `Legacy action: ${action.value}`;
  }
}

export function validateAutomationDraft(
  name: string,
  trigger: TriggerDraft,
  actions: ActionDraft[],
): string | null {
  if (
    !name.trim() ||
    new TextEncoder().encode(name.trim()).length > entityLimits.name
  )
    return `Enter a name up to ${entityLimits.name} bytes.`;
  if (trigger.type === "legacy")
    return "Select a supported trigger before saving this legacy automation.";
  if (
    trigger.type === "once" &&
    (!isValidCalendarDate(trigger.date) ||
      !isValidTimeOfDay(trigger.time) ||
      !isValidTimezoneOffsetMinutes(trigger.timezoneOffsetMinutes))
  )
    return "Choose a valid date, time, and UTC offset.";
  if (
    trigger.type === "weekly" &&
    (!Number.isInteger(trigger.weekdaysMask) ||
      trigger.weekdaysMask <= 0 ||
      trigger.weekdaysMask > 0x7f ||
      !isValidTimeOfDay(trigger.time) ||
      !isValidTimezoneOffsetMinutes(trigger.timezoneOffsetMinutes))
  )
    return "Choose at least one weekday and a valid time and UTC offset.";
  if (!actions.length || actions.length > MAX_AUTOMATION_ACTIONS)
    return "Add between one and eight actions.";
  for (const action of actions) {
    if (action.type === "legacy")
      return "Replace every legacy action before saving.";
    if (
      (action.type === "set_on_off" || action.type === "set_level") &&
      !action.deviceId
    )
      return "Select a device for every device action.";
    if (
      action.type === "set_level" &&
      (!Number.isFinite(action.level) || action.level < 0 || action.level > 100)
    )
      return "Device levels must be from 0 to 100.";
    if (action.type === "activate_scene" && !action.sceneId)
      return "Select a scene for every scene action.";
    if (
      action.type === "local_notification" &&
      (!action.message.trim() ||
        byteLength(action.message) > MAX_NOTIFICATION_BYTES ||
        action.message.includes("|"))
    )
      return `Notification messages must be 1–${entityLimits.notification} bytes.`;
    if (
      action.type === "delay" &&
      (!Number.isFinite(action.milliseconds) ||
        action.milliseconds < 0 ||
        action.milliseconds > MAX_DELAY_MS)
    )
      return "Delays must be from 0 to 60,000 ms.";
  }
  return null;
}

function byteLength(value: string): number {
  return new TextEncoder().encode(value.trim()).length;
}

function isValidCalendarDate(value: string): boolean {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value);
  if (!match) return false;
  const year = Number(match[1]);
  const month = Number(match[2]);
  const day = Number(match[3]);
  if (year === 0 || month < 1 || month > 12 || day < 1) return false;
  const daysInMonth = new Date(Date.UTC(year, month, 0)).getUTCDate();
  return day <= daysInMonth;
}

function isValidTimeOfDay(value: string): boolean {
  const match = /^(\d{2}):(\d{2})$/.exec(value);
  return Boolean(match && Number(match[1]) < 24 && Number(match[2]) < 60);
}

function isValidTimezoneOffsetMinutes(value: number): boolean {
  return (
    Number.isInteger(value) &&
    value >= MIN_TIMEZONE_OFFSET_MINUTES &&
    value <= MAX_TIMEZONE_OFFSET_MINUTES
  );
}

export function automationHasLegacyValues(automation: Automation): boolean {
  return (
    decodeTrigger(automation.trigger).type === "legacy" ||
    automation.actions.some((action) => decodeAction(action).type === "legacy")
  );
}
