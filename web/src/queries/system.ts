import { createQuery } from "@tanstack/solid-query";
import { fallbackSystem, loadSystem } from "../lib/api";

export function systemQuery() {
  return createQuery(() => ({
    queryKey: ["system"],
    queryFn: ({ signal }) => loadSystem(signal),
    placeholderData: fallbackSystem,
  }));
}
