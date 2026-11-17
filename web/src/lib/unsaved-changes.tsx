import { useBeforeLeave } from "@solidjs/router";
import { createEffect, createMemo, createSignal, onCleanup } from "solid-js";
import {
  AlertDialog,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogTitle,
} from "~/components/alert-dialog";
import { Button } from "~/components/button";

export function createUnsavedChanges(options: {
  open: () => boolean;
  dirty: () => boolean;
  label: string;
}) {
  const [pending, setPending] = createSignal<(() => void) | null>(null);
  let returnFocus: HTMLElement | null = null;
  let keepEditingButton: HTMLButtonElement | undefined;
  const dirty = createMemo(() => options.open() && options.dirty());

  function request(action: () => void) {
    if (!dirty()) {
      action();
      return;
    }
    if (pending()) return;
    returnFocus =
      document.activeElement instanceof HTMLElement
        ? document.activeElement
        : null;
    setPending(() => action);
  }

  useBeforeLeave((event) => {
    if (!dirty() || event.defaultPrevented) return;
    event.preventDefault();
    request(() => event.retry(true));
  });

  createEffect(() => {
    if (!options.open()) {
      setPending(null);
    }
  });

  createEffect(() => {
    if (!dirty()) return;
    const beforeUnload = (event: BeforeUnloadEvent) => {
      event.preventDefault();
      event.returnValue = "";
    };
    window.addEventListener("beforeunload", beforeUnload);
    onCleanup(() => window.removeEventListener("beforeunload", beforeUnload));
  });

  function Confirmation() {
    return (
      <AlertDialog
        open={pending() !== null}
        onOpenChange={(open) => !open && setPending(null)}
      >
        <AlertDialogContent
          data-testid="unsaved-changes-dialog"
          onOpenAutoFocus={(event) => {
            event.preventDefault();
            queueMicrotask(() => keepEditingButton?.focus());
          }}
          onCloseAutoFocus={(event) => {
            event.preventDefault();
            if (options.open() && returnFocus?.isConnected) returnFocus.focus();
          }}
        >
          <AlertDialogTitle>Discard unsaved changes?</AlertDialogTitle>
          <AlertDialogDescription>
            Your {options.label} changes have not been saved. Discard them to
            continue, or keep editing.
          </AlertDialogDescription>
          <div class="flex justify-end gap-2">
            <Button
              ref={(element) => (keepEditingButton = element)}
              type="button"
              variant="outline"
              onClick={() => setPending(null)}
            >
              Keep editing
            </Button>
            <Button
              type="button"
              variant="destructive"
              onClick={() => {
                const action = pending();
                setPending(null);
                action?.();
              }}
            >
              Discard changes
            </Button>
          </div>
        </AlertDialogContent>
      </AlertDialog>
    );
  }

  return { request, Confirmation };
}
