import { createSignal } from "solid-js";
import { HiOutlineExclamationTriangle, HiOutlineTrash } from "solid-icons/hi";

import {
  AlertDialog,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogTitle,
  AlertDialogTrigger,
} from "~/components/alert-dialog";
import { Button, buttonVariants } from "~/components/button";
import { Flex } from "~/components/flex";
import { Tooltip, TooltipContent, TooltipTrigger } from "~/components/tooltip";

type ConfirmDestructiveActionProps = {
  triggerLabel: string;
  triggerAriaLabel?: string;
  title: string;
  description: string;
  confirmLabel?: string;
  cancelLabel?: string;
  disabled?: boolean;
  blockedReason?: string;
  onConfirm: () => unknown | Promise<unknown>;
  onError?: (error: unknown) => void;
};

function ConfirmDestructiveAction(props: ConfirmDestructiveActionProps) {
  const [open, setOpen] = createSignal(false);
  const [pending, setPending] = createSignal(false);

  const confirm = async () => {
    setPending(true);
    try {
      await props.onConfirm();
      setOpen(false);
    } catch (error) {
      props.onError?.(error);
    } finally {
      setPending(false);
    }
  };

  return (
    <AlertDialog open={open()} onOpenChange={setOpen}>
      <Tooltip>
        <TooltipTrigger
          as={AlertDialogTrigger}
          class={buttonVariants({ variant: "destructive", size: "icon" })}
          disabled={props.disabled}
          aria-label={props.triggerAriaLabel ?? props.triggerLabel}
        >
          <HiOutlineTrash aria-hidden="true" />
        </TooltipTrigger>
        <TooltipContent>
          {props.triggerAriaLabel ?? props.triggerLabel}
        </TooltipContent>
      </Tooltip>
      <AlertDialogContent>
        <div class="flex items-start gap-3 pr-6">
          <div class="grid size-10 shrink-0 place-items-center rounded-full bg-destructive/10 text-error">
            <HiOutlineExclamationTriangle class="size-5" aria-hidden="true" />
          </div>
          <div class="space-y-2">
            <AlertDialogTitle>{props.title}</AlertDialogTitle>
            <AlertDialogDescription>{props.description}</AlertDialogDescription>
            {props.blockedReason && (
              <p
                class="rounded-md border border-warning/40 bg-warning/10 p-3 text-sm text-warning"
                role="status"
              >
                {props.blockedReason}
              </p>
            )}
          </div>
        </div>
        <Flex
          flexDirection="col-reverse"
          justifyContent="end"
          class="gap-2 sm:flex-row"
        >
          <Button
            variant="outline"
            class="w-full sm:w-auto"
            disabled={pending()}
            onClick={() => setOpen(false)}
          >
            {props.cancelLabel ?? "Cancel"}
          </Button>
          <Button
            variant="destructive"
            class="w-full sm:w-auto"
            disabled={pending() || Boolean(props.blockedReason)}
            aria-busy={pending()}
            onClick={() => void confirm()}
          >
            {props.blockedReason
              ? "Resolve dependencies first"
              : pending()
                ? "Working…"
                : (props.confirmLabel ?? "Confirm")}
          </Button>
        </Flex>
      </AlertDialogContent>
    </AlertDialog>
  );
}

export type { ConfirmDestructiveActionProps };
export { ConfirmDestructiveAction };
