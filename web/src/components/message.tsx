import type { Component, ComponentProps, ParentComponent } from "solid-js";
import { mergeProps, splitProps } from "solid-js";
import { cn } from "~/lib/utils";

type MessageProps = ComponentProps<"article"> & {
  align?: "start" | "end";
};

const Message: Component<MessageProps> = (rawProps) => {
  const props = mergeProps({ align: "start" as const }, rawProps);
  const [local, others] = splitProps(props, ["align", "class", "children"]);
  return (
    <article
      class={cn(
        "flex w-full items-end gap-2.5",
        local.align === "end" && "flex-row-reverse",
        local.class,
      )}
      {...others}
    >
      {local.children}
    </article>
  );
};

const MessageGroup: ParentComponent<ComponentProps<"div">> = (props) => {
  const [local, others] = splitProps(props, ["class", "children"]);
  return (
    <div class={cn("flex w-full flex-col gap-2", local.class)} {...others}>
      {local.children}
    </div>
  );
};

const MessageAvatar: ParentComponent<ComponentProps<"div">> = (props) => {
  const [local, others] = splitProps(props, ["class", "children"]);
  return (
    <div
      class={cn(
        "grid size-8 shrink-0 place-items-center rounded-full border bg-muted text-[0.65rem] font-semibold text-muted-foreground",
        local.class,
      )}
      {...others}
    >
      {local.children}
    </div>
  );
};

const MessageContent: ParentComponent<ComponentProps<"div">> = (props) => {
  const [local, others] = splitProps(props, ["class", "children"]);
  return (
    <div
      class={cn("flex min-w-0 max-w-[85%] flex-col gap-1", local.class)}
      {...others}
    >
      {local.children}
    </div>
  );
};

const MessageHeader: ParentComponent<ComponentProps<"div">> = (props) => {
  const [local, others] = splitProps(props, ["class", "children"]);
  return (
    <div
      class={cn("px-1 text-xs font-medium text-muted-foreground", local.class)}
      {...others}
    >
      {local.children}
    </div>
  );
};

const MessageBubble: ParentComponent<
  ComponentProps<"div"> & { tone?: "default" | "muted" }
> = (rawProps) => {
  const props = mergeProps({ tone: "muted" as const }, rawProps);
  const [local, others] = splitProps(props, ["class", "children", "tone"]);
  return (
    <div
      class={cn(
        "rounded-2xl px-3.5 py-2.5 text-sm leading-relaxed shadow-sm",
        local.tone === "default"
          ? "rounded-br-md bg-primary text-primary-foreground"
          : "rounded-bl-md border bg-muted/60 text-foreground",
        local.class,
      )}
      {...others}
    >
      {local.children}
    </div>
  );
};

const MessageFooter: ParentComponent<ComponentProps<"div">> = (props) => {
  const [local, others] = splitProps(props, ["class", "children"]);
  return (
    <div
      class={cn("px-1 text-xs text-muted-foreground", local.class)}
      {...others}
    >
      {local.children}
    </div>
  );
};

export {
  Message,
  MessageAvatar,
  MessageBubble,
  MessageContent,
  MessageFooter,
  MessageGroup,
  MessageHeader,
};
