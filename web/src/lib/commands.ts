import type { Automation, Device, Room, Scene } from "./api";
import { pages } from "./pages";

export const MAX_COMMAND_RESULTS = 12;

export type Command = {
  id: string;
  title: string;
  detail: string;
  kind: "page" | "device" | "room" | "scene" | "automation";
  action: "navigate" | "run-automation" | "unavailable";
  path?: string;
  automationId?: string;
};

export type CommandData = {
  devices: Device[];
  rooms: Room[];
  scenes: Scene[];
  automations: Automation[];
};

const deviceId = (device: Device) =>
  typeof device.id === "string" ? device.id : device.id[0];

function matches(command: Command, query: string) {
  const haystack = `${command.title} ${command.detail}`.toLowerCase();
  return haystack.includes(query.trim().toLowerCase());
}

/** Builds a bounded, local index of actions supported by the dashboard. */
export function commandsFor(data: CommandData, query = ""): Command[] {
  const commands: Command[] = [
    {
      id: "shortcut:wifi-scan",
      title: "Start Wi-Fi scan",
      detail: "Open the guided network scan",
      kind: "page",
      action: "navigate",
      path: "/system?intent=wifi-scan",
    },
    {
      id: "shortcut:diagnostics",
      title: "Open system diagnostics",
      detail: "Review network, storage, Matter, and runtime details",
      kind: "page",
      action: "navigate",
      path: "/system",
    },
    ...pages.map((page) => ({
      id: `page:${page.id}`,
      title: page.label,
      detail: page.description,
      kind: "page" as const,
      action: "navigate" as const,
      path: page.path,
    })),
    ...data.devices.map((device) => ({
      id: `device:${deviceId(device)}`,
      title: device.name,
      detail: `Device · ${device.availability}`,
      kind: "device" as const,
      action: "navigate" as const,
      path: "/devices",
    })),
    ...data.rooms.map((room) => ({
      id: `room:${room.id}`,
      title: room.name,
      detail: "Room",
      kind: "room" as const,
      action: "navigate" as const,
      path: "/rooms",
    })),
    ...data.scenes.map((scene) => ({
      id: `scene:${scene.id}`,
      title: scene.name,
      detail: "Scene · Activation unavailable — not executed",
      kind: "scene" as const,
      action: "unavailable" as const,
    })),
    ...data.automations.map((automation) => ({
      id: `automation:${automation.id}`,
      title: automation.name,
      detail:
        automation.enabled && automation.trigger === "manual"
          ? "Manual automation · Available to run"
          : automation.enabled
            ? "Automation · Not a manual automation"
            : "Automation · Disabled",
      kind: "automation" as const,
      action:
        automation.enabled && automation.trigger === "manual"
          ? ("run-automation" as const)
          : ("navigate" as const),
      path:
        automation.enabled && automation.trigger === "manual"
          ? undefined
          : "/automations",
      automationId:
        automation.enabled && automation.trigger === "manual"
          ? automation.id
          : undefined,
    })),
  ];

  return commands
    .filter((command) => matches(command, query))
    .slice(0, MAX_COMMAND_RESULTS);
}
