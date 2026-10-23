import type { ComponentProps, ParentComponent } from "solid-js";
import {
  For,
  mergeProps,
  Show,
  splitProps,
  type Component,
  type JSXElement,
} from "solid-js";

import { cn } from "~/lib/utils";

export type TimelinePropsItem = Omit<
  TimelineItemProps,
  "isActive" | "isActiveBullet" | "bulletSize" | "lineSize"
> & {
  bulletSize?: number;
};

export type TimelineProps = {
  items: TimelinePropsItem[];
  activeItem: number;
  bulletSize?: number;
  lineSize?: number;
  orientation?: "vertical" | "responsive";
  class?: string;
  "aria-label"?: string;
};

/*
  No bullet or line is active when activeItem is -1
  First bullet is active only if activeItem is 0 or more
  First line is active only if activeItem is 1 or more
*/

const Timeline: Component<TimelineProps> = (rawProps) => {
  const props = mergeProps(
    { bulletSize: 16, lineSize: 2, orientation: "vertical" as const },
    rawProps,
  );
  const responsive = () => props.orientation === "responsive";

  return (
    <ul
      aria-label={props["aria-label"]}
      class={cn(responsive() && "sm:grid sm:grid-cols-4 sm:pt-2", props.class)}
      style={{
        "padding-left": `${props.bulletSize / 2}px`,
      }}
    >
      <For each={props.items}>
        {(item, index) => (
          <TimelineItem
            title={item.title}
            description={item.description}
            bullet={item.bullet}
            isLast={index() === props.items.length - 1}
            isActive={
              props.activeItem === -1 ? false : props.activeItem >= index() + 1
            }
            isActiveBullet={
              props.activeItem === -1 ? false : props.activeItem >= index()
            }
            bulletSize={props.bulletSize}
            lineSize={props.lineSize}
            responsive={responsive()}
          />
        )}
      </For>
    </ul>
  );
};

export type TimelineItemProps = {
  title: JSXElement;
  description?: JSXElement;
  bullet?: JSXElement;
  isLast?: boolean;
  isActive: boolean;
  isActiveBullet: boolean;
  class?: string;
  bulletSize: number;
  lineSize: number;
  responsive?: boolean;
};

const TimelineItem: Component<TimelineItemProps> = (props) => {
  const [local, others] = splitProps(props, [
    "class",
    "bullet",
    "description",
    "title",
    "isLast",
    "isActive",
    "isActiveBullet",
    "bulletSize",
    "lineSize",
    "responsive",
  ]);
  return (
    <li
      class={cn(
        "relative border-l pb-8 pl-8 [border-left-width:var(--timeline-line-size)]",
        local.responsive &&
          "sm:border-l-0 sm:border-t sm:pb-0 sm:pl-0 sm:pr-6 sm:pt-8 sm:[border-top-width:var(--timeline-line-size)]",
        local.isLast && "border-l-transparent pb-0",
        local.responsive && local.isLast && "sm:border-t-transparent",
        local.isActive && !local.isLast && "border-l-primary",
        local.responsive &&
          local.isActive &&
          !local.isLast &&
          "sm:border-t-primary",
        local.class,
      )}
      style={{
        "--timeline-bullet-left": `${-local.bulletSize / 2 - local.lineSize / 2}px`,
        "--timeline-line-size": `${local.lineSize}px`,
      }}
      {...others}
    >
      <TimelineItemBullet
        lineSize={local.lineSize}
        bulletSize={local.bulletSize}
        isActive={local.isActiveBullet}
        responsive={local.responsive}
      >
        {local.bullet}
      </TimelineItemBullet>
      <TimelineItemTitle>{local.title}</TimelineItemTitle>
      <Show when={local.description}>
        <TimelineItemDescription>{local.description}</TimelineItemDescription>
      </Show>
    </li>
  );
};

export type TimelineItemBulletProps = {
  children?: JSXElement;
  isActive?: boolean;
  bulletSize: number;
  lineSize: number;
  responsive?: boolean;
};

const TimelineItemBullet: Component<TimelineItemBulletProps> = (props) => {
  return (
    <div
      class={cn(
        "absolute top-0 flex items-center justify-center rounded-full border bg-background [left:var(--timeline-bullet-left)]",
        props.responsive && "sm:left-0 sm:-translate-x-1/2 sm:-translate-y-1/2",
        props.isActive && "border-primary",
      )}
      style={{
        width: `${props.bulletSize}px`,
        height: `${props.bulletSize}px`,
        "border-width": `${props.lineSize}px`,
      }}
      aria-hidden="true"
    >
      {props.children}
    </div>
  );
};

const TimelineItemTitle: ParentComponent = (props) => {
  return (
    <div class="mb-1 text-base font-semibold leading-none">
      {props.children}
    </div>
  );
};

const TimelineItemDescription: Component<ComponentProps<"p">> = (props) => {
  const [local, others] = splitProps(props, ["class", "children"]);
  return (
    <p class={cn("text-sm text-muted-foreground", local.class)} {...others}>
      {local.children}
    </p>
  );
};

export { Timeline };
