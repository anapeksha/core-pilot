import { createQuery } from "@tanstack/solid-query";
import { fallbackTime, loadTime } from "../lib/api";

export function timeQuery() {
  return createQuery(() => ({
    queryKey: ["time"],
    queryFn: ({ signal }) => loadTime(signal),
    placeholderData: fallbackTime,
    initialDataUpdatedAt: 0,
    refetchOnMount: "always",
    staleTime: 30_000,
  }));
}
