import { describe, expect, it } from "vitest";
import { commandsFor, MAX_COMMAND_RESULTS } from "./commands";

const data = {
  devices: [
    {
      id: "lamp-1",
      name: "Kitchen lamp",
      protocol: "matter",
      capabilities: [],
      state: [],
      availability: "online",
      last_seen: 0,
    },
  ],
  rooms: [{ id: "kitchen", name: "Kitchen" }],
  scenes: [{ id: "evening", name: "Evening", actions: [] }],
  automations: [
    {
      id: "morning",
      name: "Morning routine",
      enabled: true,
      trigger: "manual",
      actions: [],
    },
    {
      id: "night",
      name: "Night routine",
      enabled: false,
      trigger: "manual",
      actions: [],
    },
  ],
};

describe("command palette index", () => {
  it("searches dynamic model commands and marks unsupported scene activation", () => {
    expect(commandsFor(data, "kitchen")).toMatchObject([
      { id: "device:lamp-1", action: "navigate", path: "/devices" },
      { id: "room:kitchen", action: "navigate", path: "/rooms" },
    ]);
    expect(commandsFor(data, "evening")).toMatchObject([
      {
        id: "scene:evening",
        action: "unavailable",
        detail: "Scene · Activation unavailable — not executed",
      },
    ]);
  });

  it("includes real system shortcuts without destructive actions", () => {
    expect(commandsFor(data, "Wi-Fi scan")).toMatchObject([
      {
        id: "shortcut:wifi-scan",
        action: "navigate",
        path: "/system?intent=wifi-scan",
      },
    ]);
    expect(commandsFor(data, "system diagnostics")).toMatchObject([
      { id: "shortcut:diagnostics", action: "navigate", path: "/system" },
    ]);
    expect(commandsFor(data, "reboot")).toEqual([]);
  });

  it("only exposes enabled manual automations as runnable", () => {
    expect(commandsFor(data, "morning")).toMatchObject([
      {
        id: "automation:morning",
        action: "run-automation",
        automationId: "morning",
      },
    ]);
    expect(commandsFor(data, "night")).toMatchObject([
      { id: "automation:night", action: "navigate", path: "/automations" },
    ]);
  });

  it("keeps results bounded", () => {
    const many = {
      ...data,
      rooms: Array.from({ length: MAX_COMMAND_RESULTS + 1 }, (_, index) => ({
        id: `room-${index}`,
        name: `Room ${index}`,
      })),
    };

    expect(commandsFor(many, "room")).toHaveLength(MAX_COMMAND_RESULTS);
  });
});
