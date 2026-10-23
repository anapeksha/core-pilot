import { createQuery } from "@tanstack/solid-query";
import { loadScenes, type ScenesResponse } from "../lib/api";

export const fallbackScenes: ScenesResponse = {
  scenes: [],
  detail: "Save scenes to activate coordinated local device states.",
};

export function scenesQuery() {
  return createQuery(() => ({
    queryKey: ["scenes"],
    queryFn: ({ signal }) => loadScenes(signal),
    placeholderData: fallbackScenes,
  }));
}
