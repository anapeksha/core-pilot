import type { ValidComponent } from "solid-js";
import { splitProps } from "solid-js";

import type { PolymorphicProps } from "@kobalte/core/polymorphic";
import * as SkeletonPrimitive from "@kobalte/core/skeleton";

import { cn } from "~/lib/utils";

type SkeletonRootProps<T extends ValidComponent = "div"> =
  SkeletonPrimitive.SkeletonRootProps<T> & { class?: string | undefined };

const Skeleton = <T extends ValidComponent = "div">(
  props: PolymorphicProps<T, SkeletonRootProps<T>>,
) => {
  const [local, others] = splitProps(props as SkeletonRootProps, [
    "class",
    "style",
  ]);
  const style = () => {
    // Clear Kobalte's inline defaults so utility classes can size the skeleton.
    const dimensions = {
      height: props.height === undefined ? "" : `${props.height}px`,
      width: props.circle
        ? props.height === undefined
          ? ""
          : `${props.height}px`
        : props.width === undefined
          ? ""
          : `${props.width}px`,
    };
    return typeof local.style === "string"
      ? `height:${dimensions.height};width:${dimensions.width};${local.style}`
      : { ...dimensions, ...local.style };
  };
  return (
    <SkeletonPrimitive.Root
      class={cn(
        "rounded-md bg-muted/80 data-[animate='true']:animate-pulse",
        local.class,
      )}
      {...others}
      style={style()}
    />
  );
};

export { Skeleton };
