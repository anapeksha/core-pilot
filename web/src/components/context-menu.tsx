import type { ValidComponent } from "solid-js";
import { createEffect, onCleanup, splitProps } from "solid-js";

import * as ContextMenuPrimitive from "@kobalte/core/context-menu";
import { useMenuContext } from "@kobalte/core/menu";
import type { PolymorphicProps } from "@kobalte/core/polymorphic";

import { cn } from "~/lib/utils";

const ContextMenu = ContextMenuPrimitive.Root;
const ContextMenuTrigger = ContextMenuPrimitive.Trigger;

type ContextMenuContentProps<T extends ValidComponent = "div"> =
  ContextMenuPrimitive.ContextMenuContentProps<T> & { class?: string };

const ContextMenuContent = <T extends ValidComponent = "div">(
  props: PolymorphicProps<T, ContextMenuContentProps<T>>,
) => {
  const [local, others] = splitProps(props as ContextMenuContentProps, [
    "class",
    "onKeyDown",
  ]);
  const menu = useMenuContext();
  createEffect(() => {
    if (!menu.isOpen()) return;
    const trigger = menu.triggerRef();
    if (!trigger) return;
    // Right-click can leave focus on the trigger until deferred menu autofocus.
    // Let Tab leave normally even during that window, without a global listener.
    const onTriggerKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Tab") menu.close(true);
    };
    trigger.addEventListener("keydown", onTriggerKeyDown, true);
    onCleanup(() =>
      trigger.removeEventListener("keydown", onTriggerKeyDown, true),
    );
  });
  return (
    <ContextMenuPrimitive.Portal>
      <ContextMenuPrimitive.Content
        class={cn(
          "z-50 min-w-44 overflow-hidden rounded-md border bg-popover p-1 text-popover-foreground shadow-md outline-none data-[expanded]:animate-in data-[closed]:animate-out data-[closed]:fade-out-0 data-[expanded]:fade-in-0 data-[closed]:zoom-out-95 data-[expanded]:zoom-in-95",
          local.class,
        )}
        {...others}
        onKeyDown={(event) => {
          const handler = local.onKeyDown;
          if (typeof handler === "function") handler(event);
          else handler?.[0](handler[1], event);
          // Tab exits the menu instead of reaching Kobalte's Tab-prevention handler.
          if (event.key === "Tab") menu.close(true);
        }}
      />
    </ContextMenuPrimitive.Portal>
  );
};

type ContextMenuItemProps<T extends ValidComponent = "div"> =
  ContextMenuPrimitive.ContextMenuItemProps<T> & { class?: string };

const ContextMenuItem = <T extends ValidComponent = "div">(
  props: PolymorphicProps<T, ContextMenuItemProps<T>>,
) => {
  const [local, others] = splitProps(props as ContextMenuItemProps, ["class"]);
  return (
    <ContextMenuPrimitive.Item
      class={cn(
        "relative flex cursor-pointer select-none items-center gap-2 rounded-sm px-2 py-1.5 text-sm outline-none data-[disabled]:pointer-events-none data-[disabled]:cursor-not-allowed data-[highlighted]:bg-accent data-[highlighted]:text-accent-foreground data-[disabled]:opacity-50",
        local.class,
      )}
      {...others}
    />
  );
};

type ContextMenuSeparatorProps<T extends ValidComponent = "hr"> =
  ContextMenuPrimitive.ContextMenuSeparatorProps<T> & { class?: string };

const ContextMenuSeparator = <T extends ValidComponent = "hr">(
  props: PolymorphicProps<T, ContextMenuSeparatorProps<T>>,
) => {
  const [local, others] = splitProps(props as ContextMenuSeparatorProps, [
    "class",
  ]);
  return (
    <ContextMenuPrimitive.Separator
      class={cn("-mx-1 my-1 h-px bg-border", local.class)}
      {...others}
    />
  );
};

export {
  ContextMenu,
  ContextMenuContent,
  ContextMenuItem,
  ContextMenuSeparator,
  ContextMenuTrigger,
};
