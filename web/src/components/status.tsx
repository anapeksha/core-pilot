import type { JSX } from "solid-js";
import { For, Show } from "solid-js";
import { Dynamic } from "solid-js/web";
import {
  HiOutlineArrowPath,
  HiOutlineCheckCircle,
  HiOutlineExclamationTriangle,
  HiOutlineInformationCircle,
  HiOutlineSignal,
  HiOutlineXMark,
} from "solid-icons/hi";
import type { IconTypes } from "solid-icons";

import { Badge } from "~/components/badge";
import { Grid } from "~/components/grid";
import { cn } from "~/lib/utils";

type StatusTone = "ok" | "warn" | "error" | "muted" | "unavailable" | "pending";

type StatusBadgeProps = {
  tone: StatusTone;
  label?: string;
  class?: string;
};

type StatusIconProps = StatusBadgeProps;

type StatusItem = StatusBadgeProps & {
  name: string;
  detail?: JSX.Element;
};

type StatusGridProps = {
  items: readonly StatusItem[];
  class?: string;
};

type StatusPresentation = {
  label: string;
  icon: IconTypes;
  class: string;
};

const STATUS_PRESENTATION: Record<StatusTone, StatusPresentation> = {
  ok: {
    label: "OK",
    icon: HiOutlineCheckCircle,
    class: "border-success/40 bg-success/10 text-success",
  },
  warn: {
    label: "Warning",
    icon: HiOutlineExclamationTriangle,
    class: "border-warning/40 bg-warning/10 text-warning",
  },
  error: {
    label: "Error",
    icon: HiOutlineXMark,
    class: "border-destructive/40 bg-destructive/10 text-error",
  },
  muted: {
    label: "Muted",
    icon: HiOutlineInformationCircle,
    class: "border-border bg-muted text-muted-foreground",
  },
  unavailable: {
    label: "Unavailable",
    icon: HiOutlineSignal,
    class: "border-border bg-background text-muted-foreground",
  },
  pending: {
    label: "Pending",
    icon: HiOutlineArrowPath,
    class: "border-border bg-secondary text-secondary-foreground",
  },
};

function StatusBadge(props: StatusBadgeProps) {
  const presentation = () => STATUS_PRESENTATION[props.tone];
  return (
    <Badge
      variant="outline"
      class={cn("gap-1.5 whitespace-nowrap", presentation().class, props.class)}
    >
      <Dynamic
        component={presentation().icon}
        class="size-3.5"
        aria-hidden="true"
      />
      {props.label ?? presentation().label}
    </Badge>
  );
}

function StatusIcon(props: StatusIconProps) {
  const presentation = () => STATUS_PRESENTATION[props.tone];
  const label = () => props.label ?? presentation().label;
  return (
    <span
      role="img"
      aria-label={label()}
      title={label()}
      class={cn(
        "inline-flex size-7 shrink-0 items-center justify-center rounded-full border",
        presentation().class,
        props.class,
      )}
    >
      <Dynamic
        component={presentation().icon}
        class="size-4"
        aria-hidden="true"
      />
    </span>
  );
}

function StatusGrid(props: StatusGridProps) {
  return (
    <Grid cols={1} colsSm={2} class={cn("gap-3", props.class)} role="list">
      <For each={props.items}>
        {(item) => (
          <div
            class="flex min-w-0 items-start justify-between gap-3 rounded-lg border bg-card p-4"
            role="listitem"
          >
            <div class="min-w-0 space-y-1">
              <p class="text-sm font-medium">{item.name}</p>
              <Show when={item.detail}>
                <div class="text-sm text-muted-foreground">{item.detail}</div>
              </Show>
            </div>
            <StatusBadge
              tone={item.tone}
              label={item.label}
              class={item.class}
            />
          </div>
        )}
      </For>
    </Grid>
  );
}

export type {
  StatusBadgeProps,
  StatusGridProps,
  StatusIconProps,
  StatusItem,
  StatusTone,
};
export { StatusBadge, StatusGrid, StatusIcon };
