import { describe, expect, it } from "vitest";
import { fallbackHealth, fallbackNetwork, fallbackSystem } from "./api";
import { boolStatus, coreStatusItems, okWhen } from "./status";

describe("status helpers", () => {
  it("formats boolean status and tone", () => {
    expect(boolStatus(true, "ready", "blocked")).toBe("ready");
    expect(boolStatus(false, "ready", "blocked")).toBe("blocked");
    expect(okWhen(true)).toBe("ok");
    expect(okWhen(false)).toBe("warn");
  });

  it("summarizes joined Wi-Fi and local access from API responses", () => {
    const items = coreStatusItems(
      { ...fallbackHealth, storage_ok: true, ai_enabled: true },
      {
        ...fallbackSystem,
        hostname: "corepilot.local",
        storage: "mounted",
        matter: "ready",
        thread: "setup needed",
        ai: "HTTP provider transport ready",
      },
      {
        ...fallbackNetwork,
        mode: "station",
        station_ssid: "Lab_WiFi",
        station_ipv4: "192.168.29.2",
      },
    );

    expect(items).toContainEqual({
      label: "Wi‑Fi",
      value: "Joined Lab_WiFi",
      tone: "ok",
    });
    expect(items).toContainEqual({
      label: "Access",
      value: "local",
      tone: "ok",
    });
    expect(items.find((item) => item.label === "Device data")?.value).toBe(
      "encrypted",
    );
  });

  it("warns when running from setup AP fallback state", () => {
    const items = coreStatusItems(
      fallbackHealth,
      fallbackSystem,
      fallbackNetwork,
    );
    expect(items.find((item) => item.label === "Wi‑Fi")).toMatchObject({
      value: "Setup network active",
      tone: "warn",
    });
    expect(items.find((item) => item.label === "Device data")).toMatchObject({
      value: "unavailable",
      tone: "warn",
    });
  });

  it("falls back to generic labels when optional firmware fields are absent", () => {
    const items = coreStatusItems(
      { ...fallbackHealth, auth: "mutual-tls", ai_enabled: false },
      {
        ...fallbackSystem,
        hostname: "",
        matter: "setup needed",
        thread: "ready",
      },
      {
        ...fallbackNetwork,
        station_ssid: null,
        station_ipv4: "192.168.29.2",
      },
    );

    expect(items).toContainEqual({
      label: "Wi‑Fi",
      value: "Joined Wi‑Fi",
      tone: "ok",
    });
    expect(items).toContainEqual({
      label: "Name",
      value: fallbackHealth.hostname,
      tone: "ok",
    });
    expect(items).toContainEqual({
      label: "Access",
      value: "mutual-tls",
      tone: "ok",
    });
    expect(items).toContainEqual({
      label: "Thread",
      value: "ready",
      tone: "ok",
    });
    expect(items.some((item) => item.label === "Assistant")).toBe(false);
  });
});
