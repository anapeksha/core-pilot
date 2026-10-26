import { For, Show, type Component } from "solid-js";
import type { IconTypes } from "solid-icons";

import { buttonVariants } from "~/components/button";
import { Tooltip, TooltipContent, TooltipTrigger } from "~/components/tooltip";
import { cn } from "~/lib/utils";

export type StepperItem = {
  label: string;
  icon: IconTypes;
};

export type StepperProps = {
  steps: readonly StepperItem[];
  activeStep: number;
  maxReachableStep?: number;
  onStepChange: (step: number) => void;
  "aria-label": string;
};

/** Compact, accessible progress navigation for bounded multi-step forms. */
export const Stepper: Component<StepperProps> = (props) => (
  <nav aria-label={props["aria-label"]}>
    <ol
      class="grid w-full items-start"
      style={{
        "grid-template-columns": `repeat(${props.steps.length}, minmax(0, 1fr))`,
      }}
    >
      <For each={props.steps}>
        {(item, index) => {
          const isActive = () => index() === props.activeStep;
          const isComplete = () => index() < props.activeStep;
          const isReachable = () =>
            index() <= (props.maxReachableStep ?? props.activeStep);
          const Icon = item.icon;

          return (
            <li class="group relative flex min-w-0 justify-center">
              <Show when={index() < props.steps.length - 1}>
                <div
                  class={cn(
                    "absolute left-[calc(50%+1.25rem)] right-[calc(-50%+1.25rem)] top-5 h-0.5 rounded-full bg-muted",
                    isComplete() && "bg-primary",
                  )}
                  aria-hidden="true"
                />
              </Show>
              <Tooltip openDelay={0}>
                <TooltipTrigger
                  as="button"
                  type="button"
                  class={cn(
                    buttonVariants({ variant: "outline", size: "icon" }),
                    "relative z-10 shrink-0 rounded-full bg-background",
                    (isActive() || isComplete()) &&
                      "border-primary bg-primary text-primary-foreground hover:bg-primary/90 hover:text-primary-foreground",
                    isActive() &&
                      "ring-2 ring-ring ring-offset-2 ring-offset-background",
                  )}
                  disabled={!isReachable()}
                  aria-label={item.label}
                  aria-current={isActive() ? "step" : undefined}
                  onClick={() => props.onStepChange(index())}
                >
                  <Icon class="size-4" aria-hidden="true" />
                  <span class="sr-only">{item.label}</span>
                </TooltipTrigger>
                <TooltipContent>{item.label}</TooltipContent>
              </Tooltip>
            </li>
          );
        }}
      </For>
    </ol>
  </nav>
);
