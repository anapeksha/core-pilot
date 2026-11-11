import { createQuery } from "@tanstack/solid-query";
import {
  fallbackMatter,
  loadMatter,
  loadMatterOperations,
  type MatterOperationPage,
} from "../lib/api";

export const fallbackMatterOperations: MatterOperationPage = {
  operations: [],
  cursor: "0",
  limit: 8,
  retained: 0,
  detail: "No Matter terminal operations are available.",
};

export function matterOperationsQuery() {
  return createQuery(() => ({
    queryKey: ["matter", "operations"],
    queryFn: ({ signal }) => loadMatterOperations(signal),
    placeholderData: fallbackMatterOperations,
    staleTime: 30_000,
  }));
}

export function matterQuery() {
  return createQuery(() => ({
    queryKey: ["matter"],
    queryFn: ({ signal }) => loadMatter(signal),
    placeholderData: fallbackMatter,
    refetchInterval: (query) =>
      query.state.data?.operation.active || query.state.data?.operation.pending
        ? 1_000
        : false,
  }));
}
