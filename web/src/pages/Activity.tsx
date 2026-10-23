import {
  createEffect,
  createMemo,
  createSignal,
  For,
  Show,
  untrack,
} from "solid-js";
import { createVirtualizer } from "@tanstack/solid-virtual";
import {
  HiOutlineArrowPath,
  HiOutlineBolt,
  HiOutlineExclamationTriangle,
} from "solid-icons/hi";

import { Button } from "~/components/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "~/components/card";
import { EmptyState } from "~/components/empty-state";
import { Flex } from "~/components/flex";
import { Grid } from "~/components/grid";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "~/components/select";
import { Skeleton } from "~/components/skeleton";
import { StatusBadge, type StatusTone } from "~/components/status";
import type { ActivityEvent } from "~/lib/api";
import {
  MAX_ACTIVITY_ITEMS,
  activityCategory,
  activityEventTypeLabel,
  activitySeverity,
  activityTimestamp,
  flattenActivityPages,
  type ActivityCategory,
  type ActivitySeverity,
} from "~/lib/activity";
import { activityPagesQuery } from "~/queries/activity";

const severityTone: Record<ActivitySeverity, StatusTone> = {
  info: "muted",
  warning: "warn",
  error: "error",
};

export default function Activity() {
  const query = activityPagesQuery();
  const [eventType, setEventType] = createSignal("all");
  const [severity, setSeverity] = createSignal<"all" | ActivitySeverity>("all");
  const [category, setCategory] = createSignal<ActivityCategory>("all");
  let activityViewport: HTMLDivElement | undefined;

  const pages = () => query.data?.pages ?? [];
  const page = () => pages()[0] ?? null;
  const events = createMemo(() => flattenActivityPages(pages()));
  const eventTypes = createMemo(() =>
    [...new Set(events().map((event) => event.kind))].sort(),
  );
  const filteredEvents = createMemo(() =>
    events().filter(
      (event) =>
        (eventType() === "all" || event.kind === eventType()) &&
        (severity() === "all" || activitySeverity(event) === severity()) &&
        (category() === "all" || activityCategory(event) === category()),
    ),
  );
  const canLoadMore = () =>
    Boolean(query.hasNextPage) && events().length < MAX_ACTIVITY_ITEMS;
  const rowVirtualizer = createVirtualizer({
    get count() {
      return filteredEvents().length;
    },
    getScrollElement: () => activityViewport ?? null,
    estimateSize: () => 88,
    initialRect: { width: 0, height: 576 },
    getItemKey: (index: number) => filteredEvents()[index]?.sequence ?? index,
    overscan: 6,
  });

  createEffect(() => {
    void filteredEvents().length;
    untrack(() => {
      rowVirtualizer.scrollToOffset(0);
      rowVirtualizer.measure();
    });
  });

  createEffect(() => {
    const lastRow = rowVirtualizer.getVirtualItems().at(-1);
    if (
      lastRow &&
      lastRow.index >= filteredEvents().length - 3 &&
      canLoadMore() &&
      !query.isFetchingNextPage
    ) {
      void query.fetchNextPage();
    }
  });

  return (
    <Card class="w-full overflow-hidden">
      <CardHeader>
        <Flex
          alignItems="start"
          justifyContent="between"
          class="flex-col gap-4 sm:flex-row"
        >
          <div class="max-w-3xl">
            <div class="mb-3 h-px w-12 bg-foreground" />
            <h1 class="text-xl font-semibold tracking-tight">Activity</h1>
            <CardDescription>
              Newest encrypted firmware events first. Event details never
              include credentials or request content.
            </CardDescription>
          </div>
        </Flex>
      </CardHeader>
      <CardContent>
        <Show
          when={!query.isFetching || query.data}
          fallback={<ActivitySkeleton />}
        >
          <Show
            when={!query.isError}
            fallback={
              <EmptyState
                icon={<HiOutlineExclamationTriangle />}
                title="Activity is unavailable"
                description="The encrypted activity history could not be loaded. Try again when storage is available."
                action={
                  <Button
                    type="button"
                    variant="outline"
                    onClick={() => query.refetch()}
                  >
                    Try again
                  </Button>
                }
              />
            }
          >
            <Grid cols={1} colsLg={3} class="items-start gap-4">
              <Card class="lg:col-span-2">
                <CardHeader>
                  <CardTitle>Event history</CardTitle>
                  <CardDescription>
                    {page()?.detail ?? "Loading firmware activity."}
                  </CardDescription>
                </CardHeader>
                <CardContent class="space-y-4">
                  <Show
                    when={filteredEvents().length > 0}
                    fallback={
                      <EmptyState
                        icon={<HiOutlineBolt />}
                        title={
                          events().length
                            ? "No matching events"
                            : "No activity yet"
                        }
                        description={
                          events().length
                            ? "Change or clear filters to see loaded activity."
                            : "Core Pilot will retain operational events as they occur."
                        }
                      />
                    }
                  >
                    <p id="activity-list-summary" class="sr-only">
                      {filteredEvents().length} loaded events, newest first.
                      Scroll to load older events.
                    </p>
                    <div
                      ref={(element) => (activityViewport = element)}
                      class="h-144 max-h-[70vh] overflow-auto rounded-md focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2"
                      tabindex="0"
                      role="region"
                      aria-label="Scrollable activity history"
                    >
                      <ol
                        class="relative m-0 w-full list-none p-0"
                        style={{ height: `${rowVirtualizer.getTotalSize()}px` }}
                        aria-label="Activity events"
                        aria-describedby="activity-list-summary"
                      >
                        <For each={rowVirtualizer.getVirtualItems()}>
                          {(virtualRow) => {
                            const event = () =>
                              filteredEvents()[virtualRow.index];
                            return (
                              <Show when={event()}>
                                {(currentEvent) => (
                                  <li
                                    ref={(element) =>
                                      queueMicrotask(() =>
                                        rowVirtualizer.measureElement(element),
                                      )
                                    }
                                    data-index={virtualRow.index}
                                    class="absolute top-0 left-0 w-full pb-2"
                                    style={{
                                      transform: `translateY(${virtualRow.start}px)`,
                                    }}
                                  >
                                    <ActivityRow
                                      event={currentEvent()}
                                      position={virtualRow.index + 1}
                                      total={filteredEvents().length}
                                    />
                                  </li>
                                )}
                              </Show>
                            );
                          }}
                        </For>
                      </ol>
                    </div>
                  </Show>
                  <Show when={query.isFetchNextPageError}>
                    <p class="text-sm text-destructive" role="alert">
                      Older activity could not load. Retrying is safe.
                    </p>
                  </Show>
                  <Show when={canLoadMore()}>
                    <Button
                      type="button"
                      variant="outline"
                      disabled={query.isFetchingNextPage}
                      onClick={() => void query.fetchNextPage()}
                    >
                      <Show when={query.isFetchingNextPage}>
                        <HiOutlineArrowPath
                          class="animate-spin motion-reduce:animate-none"
                          data-icon="inline-start"
                        />
                      </Show>
                      {query.isFetchingNextPage
                        ? "Loading older…"
                        : "Load older"}
                    </Button>
                  </Show>
                </CardContent>
              </Card>

              <Card>
                <CardHeader>
                  <CardTitle>Filters</CardTitle>
                  <CardDescription>
                    Filters use only each event’s type and detail.
                  </CardDescription>
                </CardHeader>
                <CardContent class="space-y-4">
                  <FilterSelect
                    label="Event type"
                    value={eventType()}
                    onChange={setEventType}
                    options={[
                      { value: "all", label: "All event types" },
                      ...eventTypes().map((kind) => ({
                        value: kind,
                        label: activityEventTypeLabel(kind),
                      })),
                    ]}
                  />
                  <FilterSelect
                    label="Severity"
                    value={severity()}
                    onChange={(value) =>
                      setSeverity(value as "all" | ActivitySeverity)
                    }
                    options={[
                      { value: "all", label: "All severities" },
                      { value: "info", label: "Info" },
                      { value: "warning", label: "Warning" },
                      { value: "error", label: "Error" },
                    ]}
                  />
                  <FilterSelect
                    label="Related category"
                    value={category()}
                    onChange={(value) => setCategory(value as ActivityCategory)}
                    options={[
                      { value: "all", label: "All categories" },
                      { value: "automation", label: "Automation" },
                      { value: "ai", label: "AI tools" },
                      { value: "network", label: "Network and time" },
                      { value: "ota", label: "Firmware updates" },
                      { value: "configuration", label: "Configuration" },
                      { value: "other", label: "Other" },
                    ]}
                  />
                  <p class="text-xs text-muted-foreground">
                    Room and device filters are unavailable until activity
                    events carry room or device IDs.
                  </p>
                </CardContent>
              </Card>
            </Grid>

            <p class="mt-4 text-sm text-muted-foreground" aria-live="polite">
              {page()?.retained ?? 0} retained events
              {page()?.dropped_events
                ? ` · ${page()!.dropped_events} dropped`
                : ""}
              {` · ${events().length} loaded`}
              {events().length >= MAX_ACTIVITY_ITEMS
                ? ` · Keeping the newest ${MAX_ACTIVITY_ITEMS}`
                : ""}
            </p>
          </Show>
        </Show>
      </CardContent>
    </Card>
  );
}

function ActivityRow(props: {
  event: ActivityEvent;
  position: number;
  total: number;
}) {
  const severity = () => activitySeverity(props.event);
  return (
    <article
      class="flex items-start justify-between gap-3 rounded-md border p-3"
      aria-label={`Event ${props.position} of ${props.total}: ${activityEventTypeLabel(props.event.kind)}`}
    >
      <div class="min-w-0 space-y-1">
        <p class="text-sm font-medium capitalize">
          {activityEventTypeLabel(props.event.kind)}
        </p>
        <p class="text-sm text-muted-foreground">{props.event.detail}</p>
        <p class="text-xs text-muted-foreground">
          {activityTimestamp(props.event)} · Sequence {props.event.sequence}
        </p>
      </div>
      <StatusBadge tone={severityTone[severity()]} label={severity()} />
    </article>
  );
}

type FilterOption = { value: string; label: string };

function FilterSelect(props: {
  label: string;
  value: string;
  onChange: (value: string) => void;
  options: FilterOption[];
}) {
  return (
    <Select<FilterOption>
      options={props.options}
      optionValue="value"
      optionTextValue="label"
      value={
        props.options.find((option) => option.value === props.value) ?? null
      }
      onChange={(option) => option && props.onChange(option.value)}
      itemComponent={(itemProps) => (
        <SelectItem item={itemProps.item}>
          {itemProps.item.rawValue.label}
        </SelectItem>
      )}
    >
      <SelectLabel>{props.label}</SelectLabel>
      <SelectTrigger class="mt-2">
        <SelectValue<FilterOption>>
          {(state) => state.selectedOption()?.label}
        </SelectValue>
      </SelectTrigger>
      <SelectContent />
    </Select>
  );
}

export function ActivitySkeleton() {
  return (
    <div role="status" aria-busy="true" aria-live="polite">
      <span class="sr-only">Loading activity</span>
      <div aria-hidden="true">
        <Grid cols={1} colsLg={3} class="items-start gap-4">
          <Card class="lg:col-span-2">
            <CardHeader class="gap-2">
              <Skeleton class="h-5 w-32 rounded-md bg-muted" />
              <Skeleton class="h-4 w-80 max-w-full rounded-md bg-muted" />
            </CardHeader>
            <CardContent class="space-y-2">
              <For each={["w-36", "w-44", "w-32", "w-40"]}>
                {(titleWidth) => (
                  <div
                    class="flex min-h-20 items-start justify-between gap-3 rounded-md border bg-muted/20 p-3"
                    data-activity-skeleton-row
                  >
                    <div class="min-w-0 flex-1 space-y-2">
                      <Skeleton
                        class={`h-4 rounded-md bg-muted ${titleWidth}`}
                      />
                      <Skeleton class="h-4 w-full max-w-md rounded-md bg-muted" />
                      <Skeleton class="h-3 w-44 max-w-full rounded-md bg-muted" />
                    </div>
                    <Skeleton class="h-6 w-16 shrink-0 rounded-full bg-muted" />
                  </div>
                )}
              </For>
            </CardContent>
          </Card>
          <Card>
            <CardHeader class="gap-2">
              <Skeleton class="h-5 w-20 rounded-md bg-muted" />
              <Skeleton class="h-4 w-full max-w-64 rounded-md bg-muted" />
            </CardHeader>
            <CardContent class="space-y-4">
              <For each={["w-24", "w-20", "w-32"]}>
                {(labelWidth) => (
                  <div class="space-y-2" data-activity-skeleton-filter>
                    <Skeleton class={`h-4 rounded-md bg-muted ${labelWidth}`} />
                    <Skeleton class="h-10 w-full rounded-md bg-muted" />
                  </div>
                )}
              </For>
              <Skeleton class="h-3 w-full max-w-72 rounded-md bg-muted" />
            </CardContent>
          </Card>
        </Grid>
        <Skeleton class="mt-4 h-4 w-48 max-w-full rounded-md bg-muted" />
      </div>
    </div>
  );
}
