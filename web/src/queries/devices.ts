import { createQuery } from "@tanstack/solid-query";
import { loadDevices, type DevicesResponse } from "../lib/api";

export const fallbackDevices: DevicesResponse = {
  devices: [],
  detail: "Commission Matter devices to build your local device registry.",
};

export const deviceRefreshIntervalMs = 2_000;

export function devicesQuery() {
  return createQuery(() => ({
    queryKey: ["devices"],
    queryFn: ({ signal }) => loadDevices(signal),
    placeholderData: fallbackDevices,
    refetchInterval: deviceRefreshIntervalMs,
    refetchIntervalInBackground: false,
  }));
}
