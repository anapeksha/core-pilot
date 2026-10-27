import { createQuery } from "@tanstack/solid-query";
import { fallbackTime, loadTime, type TimeStatus } from "../lib/api";

export function timeRefetchInterval(status: TimeStatus | undefined) {
  return status?.ntp === "syncing" ? 2_000 : false;
}

export function timeQuery() {
  return createQuery(() => ({
    queryKey: ["time"],
    queryFn: ({ signal }) => loadTime(signal),
    placeholderData: fallbackTime,
    initialDataUpdatedAt: 0,
    refetchOnMount: "always",
    refetchInterval: (query) =>
      timeRefetchInterval(query.state.data as TimeStatus | undefined),
    staleTime: 30_000,
  }));
}
