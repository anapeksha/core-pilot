import type { Component, ComponentProps } from "solid-js";

import { Toaster as Sonner } from "solid-sonner";

type ToasterProps = ComponentProps<typeof Sonner>;

const Toaster: Component<ToasterProps> = (props) => {
  return (
    <Sonner
      class="toaster group"
      toastOptions={{
        classes: {
          toast:
            "group toast rounded-xl group-[.toaster]:border-border/80 group-[.toaster]:bg-card/95 group-[.toaster]:text-card-foreground group-[.toaster]:shadow-2xl group-[.toaster]:shadow-black/20 group-[.toaster]:backdrop-blur-xl",
          title: "group-[.toast]:font-semibold group-[.toast]:tracking-tight",
          description:
            "group-[.toast]:text-muted-foreground group-[.toast]:leading-relaxed",
          actionButton:
            "group-[.toast]:rounded-md group-[.toast]:bg-primary group-[.toast]:text-primary-foreground",
          cancelButton:
            "group-[.toast]:rounded-md group-[.toast]:bg-muted group-[.toast]:text-muted-foreground",
          closeButton:
            "group-[.toast]:border-border group-[.toast]:bg-background group-[.toast]:text-muted-foreground",
        },
      }}
      {...props}
    />
  );
};

export { Toaster };
