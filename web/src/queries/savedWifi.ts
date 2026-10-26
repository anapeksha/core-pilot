import { createQuery } from "@tanstack/solid-query";
import { loadSavedWifiNetworks } from "../lib/api";

export function savedWifiQuery() {
  return createQuery(() => ({
    queryKey: ["network", "wifi", "saved"],
    queryFn: ({ signal }) => loadSavedWifiNetworks(signal),
  }));
}
