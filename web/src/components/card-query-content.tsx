import { type JSX, Show } from "solid-js";
import { Button } from "~/components/button";
import { CardContent } from "~/components/card";

type CardQuery = {
  data: unknown;
  error: unknown;
  isPlaceholderData: boolean;
  isFetching: boolean;
  refetch: () => Promise<unknown>;
};

export function CardQueryContent(props: {
  queries: readonly CardQuery[];
  label: string;
  fallback: JSX.Element;
  children: JSX.Element;
}) {
  let content: HTMLDivElement | undefined;
  const hasData = () =>
    props.queries.every(
      (query) => query.data !== undefined && !query.isPlaceholderData,
    );
  const error = () => props.queries.find((query) => query.error)?.error;
  const refreshing = () => props.queries.some((query) => query.isFetching);
  const errorMessage = () => {
    const cause = error();
    return cause instanceof Error
      ? cause.message
      : "The firmware did not return this data.";
  };
  const retry = async (event: MouseEvent) => {
    const trigger = event.currentTarget as HTMLButtonElement;
    const hadFocus = document.activeElement === trigger;
    try {
      await Promise.all(props.queries.map((query) => query.refetch()));
    } finally {
      if (hadFocus && document.activeElement === document.body) {
        if (trigger.isConnected) trigger.focus();
        else content?.focus();
      }
    }
  };

  return (
    <CardContent
      ref={(element) => (content = element)}
      tabindex="-1"
      role="group"
      aria-label={`${props.label} content`}
      class="focus-visible:outline-2 focus-visible:outline-ring"
    >
      <Show
        when={hasData()}
        fallback={
          <Show
            when={error()}
            fallback={
              <div
                role="status"
                aria-busy="true"
                data-home-card-skeleton={props.label}
              >
                <p class="mb-3 text-xs text-muted-foreground">
                  Loading {props.label}…
                </p>
                <div aria-hidden="true">{props.fallback}</div>
              </div>
            }
          >
            <div
              role="alert"
              class="space-y-2 rounded-md border border-destructive/40 p-3"
            >
              <p class="text-sm font-medium">Could not load {props.label}</p>
              <p class="wrap-break-word text-xs text-muted-foreground">
                {errorMessage()}
              </p>
              <Button
                size="sm"
                variant="outline"
                disabled={refreshing()}
                onClick={retry}
              >
                Retry {props.label}
              </Button>
            </div>
          </Show>
        }
      >
        <Show
          when={error()}
          fallback={
            <Show when={refreshing()}>
              <p role="status" class="mb-3 text-xs text-muted-foreground">
                Refreshing {props.label}…
              </p>
            </Show>
          }
        >
          <div
            role="alert"
            class="mb-3 space-y-2 rounded-md border border-warning/40 p-3"
          >
            <p class="text-sm font-medium">Could not refresh {props.label}</p>
            <p class="text-xs text-muted-foreground">
              Showing previously loaded data; it may be out of date.
            </p>
            <p class="wrap-break-word text-xs text-muted-foreground">
              {errorMessage()}
            </p>
            <Button
              size="sm"
              variant="outline"
              disabled={refreshing()}
              onClick={retry}
            >
              {refreshing() ? "Retrying…" : `Retry ${props.label}`}
            </Button>
          </div>
        </Show>
        {props.children}
      </Show>
    </CardContent>
  );
}
