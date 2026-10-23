import type { JSX } from "solid-js";

import { Card, CardContent } from "~/components/card";
import { Flex } from "~/components/flex";
import { cn } from "~/lib/utils";

type EmptyStateProps = {
  title: string;
  description?: string;
  icon?: JSX.Element;
  action?: JSX.Element;
  class?: string;
};

function EmptyState(props: EmptyStateProps) {
  return (
    <Card class={cn("border-dashed", props.class)}>
      <CardContent class="py-10 sm:py-12">
        <Flex
          flexDirection="col"
          justifyContent="center"
          class="mx-auto max-w-md gap-4 text-center"
        >
          {props.icon && (
            <div
              class="grid size-12 place-items-center rounded-full bg-muted text-muted-foreground [&_svg]:size-6"
              aria-hidden="true"
            >
              {props.icon}
            </div>
          )}
          <div class="space-y-1.5">
            <h2 class="text-base font-semibold">{props.title}</h2>
            {props.description && (
              <p class="text-sm text-muted-foreground">{props.description}</p>
            )}
          </div>
          {props.action && <div class="pt-1">{props.action}</div>}
        </Flex>
      </CardContent>
    </Card>
  );
}

export type { EmptyStateProps };
export { EmptyState };
