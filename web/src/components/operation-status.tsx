import type { JSX } from "solid-js";
import { Show } from "solid-js";

import { Flex } from "~/components/flex";
import { Progress } from "~/components/progress";
import { Spinner } from "~/components/spinner";
import { cn } from "~/lib/utils";

type OperationStatusProps = {
  title: string;
  description?: string;
  current?: number;
  total?: number;
  unit?: string;
  active?: boolean;
  trailing?: JSX.Element;
  class?: string;
};

function OperationStatus(props: OperationStatusProps) {
  const hasMeasuredProgress = () =>
    Number.isFinite(props.current) &&
    Number.isFinite(props.total) &&
    (props.current ?? -1) >= 0 &&
    (props.total ?? 0) > 0;

  const measuredCurrent = () =>
    Math.min(props.current ?? 0, props.total ?? Number.MAX_SAFE_INTEGER);

  return (
    <section
      class={cn("space-y-3 rounded-lg border bg-card p-4", props.class)}
      aria-label={props.title}
      aria-live="polite"
    >
      <Flex alignItems="start" class="gap-3">
        <div class="min-w-0 flex-1 space-y-1">
          <Flex justifyContent="start" class="gap-2">
            <Show when={props.active}>
              <Spinner />
            </Show>
            <h3 class="font-medium leading-5">{props.title}</h3>
          </Flex>
          <Show when={props.description}>
            <p class="text-sm text-muted-foreground">{props.description}</p>
          </Show>
        </div>
        <Show when={props.trailing}>
          <div class="shrink-0">{props.trailing}</div>
        </Show>
      </Flex>

      <Show when={hasMeasuredProgress()}>
        <div class="space-y-2">
          <Flex class="gap-4 text-xs text-muted-foreground">
            <span>Progress</span>
            <span class="tabular-nums">
              {measuredCurrent()} / {props.total} {props.unit}
            </span>
          </Flex>
          <Progress
            value={measuredCurrent()}
            minValue={0}
            maxValue={props.total}
            aria-label={`${props.title} progress`}
          />
        </div>
      </Show>
    </section>
  );
}

export type { OperationStatusProps };
export { OperationStatus };
