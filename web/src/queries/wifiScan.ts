import { createQuery } from "@tanstack/solid-query";
import { fallbackWifiScan, runWifiScanSession } from "../lib/api";

export function wifiScanQuery() {
  return createQuery(() => ({
    queryKey: ["network", "wifi", "scan"],
    queryFn: ({ signal }) => runWifiScanSession(signal),
    placeholderData: fallbackWifiScan,
    enabled: false,
    staleTime: 30_000,
  }));
}
