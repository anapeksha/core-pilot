import type { JSX, ValidComponent } from "solid-js";
import { splitProps, type Component } from "solid-js";

import type { PolymorphicProps } from "@kobalte/core/polymorphic";
import * as TooltipPrimitive from "@kobalte/core/tooltip";

import { Button, type ButtonProps } from "~/components/button";
import { cn } from "~/lib/utils";

const TooltipTrigger = TooltipPrimitive.Trigger;

const Tooltip: Component<TooltipPrimitive.TooltipRootProps> = (props) => {
  return <TooltipPrimitive.Root gutter={4} {...props} />;
};

type TooltipContentProps<T extends ValidComponent = "div"> =
  TooltipPrimitive.TooltipContentProps<T> & { class?: string | undefined };

type TooltipIconButtonProps = Omit<
  JSX.ButtonHTMLAttributes<HTMLButtonElement>,
  "type"
> &
  Pick<ButtonProps, "class" | "size" | "variant"> & {
    "aria-label": string;
    children: JSX.Element;
    tooltip?: JSX.Element;
    type?: "button" | "reset" | "submit";
  };

const TooltipIconButton: Component<TooltipIconButtonProps> = (props) => {
  const [local, buttonProps] = splitProps(props, [
    "aria-label",
    "children",
    "tooltip",
  ]);

  return (
    <Tooltip>
      <TooltipTrigger
        as={Button}
        aria-label={local["aria-label"]}
        {...buttonProps}
      >
        {local.children}
      </TooltipTrigger>
      <TooltipContent>{local.tooltip ?? local["aria-label"]}</TooltipContent>
    </Tooltip>
  );
};

const TooltipContent = <T extends ValidComponent = "div">(
  props: PolymorphicProps<T, TooltipContentProps<T>>,
) => {
  const [local, others] = splitProps(props as TooltipContentProps, ["class"]);
  return (
    <TooltipPrimitive.Portal>
      <TooltipPrimitive.Content
        class={cn(
          "z-50 origin-[var(--kb-popover-content-transform-origin)] overflow-hidden rounded-md border bg-popover px-3 py-1.5 text-sm text-popover-foreground shadow-md animate-in fade-in-0 zoom-in-95",
          local.class,
        )}
        {...others}
      />
    </TooltipPrimitive.Portal>
  );
};

export { Tooltip, TooltipTrigger, TooltipContent, TooltipIconButton };
