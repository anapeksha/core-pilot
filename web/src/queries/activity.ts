import { createInfiniteQuery } from "@tanstack/solid-query";
import { loadActivityPage } from "~/lib/api";
import { ACTIVITY_PAGE_LIMIT } from "~/lib/activity";

export function activityPagesQuery() {
  return createInfiniteQuery(() => ({
    queryKey: ["activity", "pages", ACTIVITY_PAGE_LIMIT],
    initialPageParam: null as number | null,
    queryFn: ({ pageParam, signal }) =>
      loadActivityPage(
        { before_sequence: pageParam, limit: ACTIVITY_PAGE_LIMIT },
        signal,
      ),
    getNextPageParam: (page) => page.next_before_sequence ?? undefined,
  }));
}
