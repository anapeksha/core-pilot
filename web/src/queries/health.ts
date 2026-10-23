import { createQuery } from "@tanstack/solid-query";
import { fallbackHealth, loadHealth } from "../lib/api";

export function healthQuery() {
  return createQuery(() => ({
    queryKey: ["health"],
    queryFn: ({ signal }) => loadHealth(signal),
    placeholderData: fallbackHealth,
  }));
}
