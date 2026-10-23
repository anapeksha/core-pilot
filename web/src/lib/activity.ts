import type { ActivityEvent, ActivityPage } from "~/lib/api";

export const ACTIVITY_PAGE_LIMIT = 20;
export const MAX_ACTIVITY_ITEMS = 1_000;

export type ActivitySeverity = "info" | "warning" | "error";
export type ActivityCategory =
  "all" | "automation" | "ai" | "network" | "ota" | "configuration" | "other";

export function appendActivityPage(
  current: ActivityEvent[],
  page: ActivityPage,
): ActivityEvent[] {
  const seen = new Set(current.map((event) => event.sequence));
  return [
    ...current,
    ...page.events.filter((event) => !seen.has(event.sequence)),
  ]
    .sort((left, right) => right.sequence - left.sequence)
    .slice(0, MAX_ACTIVITY_ITEMS);
}

export function flattenActivityPages(pages: ActivityPage[]): ActivityEvent[] {
  return pages.reduce(appendActivityPage, []);
}

export function activitySeverity(event: ActivityEvent): ActivitySeverity {
  const value = `${event.kind} ${event.detail}`.toLowerCase();
  if (/(failed|failure|rejected|queue_full|disconnected|error)/.test(value))
    return "error";
  if (/(warning|warn|unavailable|pending|stale)/.test(value)) return "warning";
  return "info";
}

export function activityCategory(event: ActivityEvent): ActivityCategory {
  const value = `${event.kind} ${event.detail}`.toLowerCase();
  if (value.includes("automation")) return "automation";
  if (value.includes("ai_tool")) return "ai";
  if (/(wifi|time_sync)/.test(value)) return "network";
  if (value.includes("ota")) return "ota";
  if (/(configuration|config)/.test(value)) return "configuration";
  return "other";
}

export function activityEventTypeLabel(kind: string): string {
  return kind.replaceAll("_", " ");
}

export function activityTimestamp(event: ActivityEvent): string {
  if (event.unix_seconds !== null) {
    return new Date(event.unix_seconds * 1000).toLocaleString();
  }
  return `Uptime ${formatDuration(event.uptime_seconds)}`;
}

function formatDuration(seconds: number): string {
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const remainder = seconds % 60;
  if (hours > 0) return `${hours}h ${minutes}m`;
  if (minutes > 0) return `${minutes}m ${remainder}s`;
  return `${remainder}s`;
}
