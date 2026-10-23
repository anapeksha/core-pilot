import { createQuery } from "@tanstack/solid-query";
import { fallbackNetwork, loadNetwork } from "../lib/api";

export function networkQuery() {
  return createQuery(() => ({
    queryKey: ["network"],
    queryFn: ({ signal }) => loadNetwork(signal),
    placeholderData: fallbackNetwork,
  }));
}
