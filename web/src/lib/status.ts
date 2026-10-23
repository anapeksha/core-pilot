import type { Health, NetworkStatus, SystemStatus, Tone } from "./api";
export { fallbackHealth, loadHealth } from "./api";

export type StatusItem = {
  label: string;
  value: string;
  tone?: Tone;
};

export function boolStatus(value: boolean, yes = "yes", no = "no"): string {
  return value ? yes : no;
}

export function okWhen(value: boolean): Tone {
  return value ? "ok" : "warn";
}

export function coreStatusItems(
  health: Health,
  system: SystemStatus,
  network: NetworkStatus,
): StatusItem[] {
  const joinedWifi = Boolean(network.station_ipv4);
  return [
    {
      label: "Wi‑Fi",
      value: joinedWifi
        ? `Joined ${network.station_ssid ?? "Wi‑Fi"}`
        : "Setup network active",
      tone: joinedWifi ? "ok" : "warn",
    },
    { label: "Name", value: system.hostname || health.hostname, tone: "ok" },
    {
      label: "Saved settings",
      value: system.storage,
      tone: health.storage_ok ? "ok" : "warn",
    },
    {
      label: "Access",
      value: health.auth.includes("none") ? "local" : health.auth,
      tone: "ok",
    },
    {
      label: "Matter",
      value: system.matter,
      tone: system.matter.includes("ready") ? "ok" : "warn",
    },
    {
      label: "Thread",
      value: system.thread,
      tone: system.thread.includes("ready") ? "ok" : "muted",
    },

    {
      label: "Device data",
      value: health.storage_ok ? "encrypted" : "unavailable",
      tone: health.storage_ok ? "ok" : "warn",
    },
  ];
}
