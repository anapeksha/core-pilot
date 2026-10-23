import { createQuery } from "@tanstack/solid-query";
import { fallbackMatter, loadMatter } from "../lib/api";

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
