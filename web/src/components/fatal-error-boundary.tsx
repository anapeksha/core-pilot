import { ErrorBoundary, Show, onMount, type ParentProps } from "solid-js";
import {
  HiOutlineArrowPath,
  HiOutlineExclamationTriangle,
} from "solid-icons/hi";
import { toast } from "solid-sonner";

import { Button } from "~/components/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardFooter,
  CardHeader,
  CardTitle,
} from "~/components/card";

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function FatalErrorFallback(props: { error: unknown; reset: () => void }) {
  const message = errorMessage(props.error);

  onMount(() => {
    console.error("Core Pilot dashboard render failure", props.error);
    toast.error("The dashboard could not finish loading.", {
      id: "fatal-dashboard-error",
      description: "Local device services continue running independently.",
    });
  });

  return (
    <main class="grid min-h-svh place-items-center bg-background px-4 py-10 text-foreground">
      <Card class="w-full max-w-lg overflow-hidden shadow-2xl shadow-black/20">
        <div class="h-1 bg-destructive" aria-hidden="true" />
        <CardHeader class="gap-4 p-6 sm:p-8">
          <div class="grid size-12 place-items-center rounded-xl border border-destructive/30 bg-destructive/10 text-destructive">
            <HiOutlineExclamationTriangle class="size-6" aria-hidden="true" />
          </div>
          <div class="space-y-2">
            <CardTitle class="text-xl">Dashboard unavailable</CardTitle>
            <CardDescription class="max-w-md leading-relaxed">
              Core Pilot encountered a display error. Local automations and
              device control continue independently of this dashboard.
            </CardDescription>
          </div>
        </CardHeader>
        <CardContent class="px-6 pb-2 sm:px-8">
          <div class="space-y-3 rounded-lg border bg-muted/40 p-4 text-sm text-muted-foreground">
            <p>
              Try restoring the current view first. Reload only if the problem
              continues.
            </p>
            <Show when={import.meta.env.DEV}>
              <p class="wrap-break-word border-t pt-3 font-mono text-xs text-foreground/70">
                {message}
              </p>
            </Show>
          </div>
        </CardContent>
        <CardFooter class="flex-col-reverse gap-2 p-6 sm:flex-row sm:justify-end sm:p-8">
          <Button
            type="button"
            variant="outline"
            class="w-full sm:w-auto"
            onClick={() => window.location.reload()}
          >
            Reload dashboard
          </Button>
          <Button type="button" class="w-full sm:w-auto" onClick={props.reset}>
            <HiOutlineArrowPath data-icon="inline-start" />
            Try again
          </Button>
        </CardFooter>
      </Card>
    </main>
  );
}

export function FatalErrorBoundary(props: ParentProps) {
  return (
    <ErrorBoundary
      fallback={(error, reset) => (
        <FatalErrorFallback error={error} reset={reset} />
      )}
    >
      {props.children}
    </ErrorBoundary>
  );
}
