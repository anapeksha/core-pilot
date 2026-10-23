import { createQuery } from "@tanstack/solid-query";
import { loadGroups, type GroupsResponse } from "../lib/api";

export const fallbackGroups: GroupsResponse = {
  groups: [],
  detail: "Create groups to control multiple devices together.",
};

export function groupsQuery() {
  return createQuery(() => ({
    queryKey: ["groups"],
    queryFn: ({ signal }) => loadGroups(signal),
    placeholderData: fallbackGroups,
  }));
}
