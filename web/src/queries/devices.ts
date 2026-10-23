import { createQuery } from "@tanstack/solid-query";
import { loadDevices, type DevicesResponse } from "../lib/api";

export const fallbackDevices: DevicesResponse = {
  devices: [],
  detail: "Commission Matter devices to build your local device registry.",
};

export function devicesQuery() {
  return createQuery(() => ({
    queryKey: ["devices"],
    queryFn: ({ signal }) => loadDevices(signal),
    placeholderData: fallbackDevices,
  }));
}
