import { createEffect, createSignal, For, Show } from "solid-js";
import { createForm } from "solid-hook-form";
import {
  HiOutlineArrowPath,
  HiOutlineBeaker,
  HiOutlineCheck,
  HiOutlineInformationCircle,
  HiOutlineSignal,
  HiOutlineWifi,
} from "solid-icons/hi";
import { toast } from "solid-sonner";
import { z } from "zod";

import {
  Accordion,
  AccordionContent,
  AccordionItem,
  AccordionTrigger,
} from "~/components/accordion";
import { Alert } from "~/components/alert";
import { Button } from "~/components/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardFooter,
  CardHeader,
  CardTitle,
} from "~/components/card";
import { Flex } from "~/components/flex";
import { Grid } from "~/components/grid";
import { OperationStatus } from "~/components/operation-status";
import {
  Sheet,
  SheetContent,
  SheetDescription,
  SheetFooter,
  SheetHeader,
  SheetTitle,
} from "~/components/sheet";
import { Skeleton } from "~/components/skeleton";
import { Stepper } from "~/components/stepper";
import {
  TextField,
  TextFieldDescription,
  TextFieldErrorMessage,
  TextFieldInput,
  TextFieldLabel,
} from "~/components/text-field";
import { Tooltip, TooltipContent, TooltipTrigger } from "~/components/tooltip";
import { fallbackMatter, type MatterTransport } from "~/lib/api";
import { cn } from "~/lib/utils";
import { boolStatus, okWhen, type StatusItem } from "~/lib/status";
import { createToastGate } from "~/lib/toast";
import {
  matterCommissionCancelMutation,
  matterCommissionMutation,
} from "~/mutations/matter";
import { matterQuery as createMatterQuery } from "~/queries/matter";

const setupCodeSchema = z
  .string()
  .trim()
  .min(1, "Enter a setup code.")
  .max(512, "Setup codes must be 512 characters or fewer.");

const commissioningSchema = z.object({
  setupCode: setupCodeSchema,
});

const wifiCommissioningSchema = commissioningSchema;
const threadCommissioningSchema = commissioningSchema;

type WifiCommissioningValues = z.input<typeof wifiCommissioningSchema>;
type ThreadCommissioningValues = z.input<typeof threadCommissioningSchema>;
type CommissioningSheet = MatterTransport | null;

function statusToneClass(tone: StatusItem["tone"]): string {
  return tone === "muted" ? "text-muted-foreground" : "text-foreground";
}

function StatusGrid(props: { items: StatusItem[] }) {
  return (
    <Grid colsMd={2} class="gap-3">
      <For each={props.items}>
        {(item) => (
          <Card class="min-h-24">
            <CardContent class="flex h-full flex-col justify-center gap-1 pt-6">
              <p class="text-xs font-medium text-muted-foreground">
                {item.label}
              </p>
              <p
                class={cn(
                  "text-base font-semibold",
                  statusToneClass(item.tone),
                )}
              >
                {item.value}
              </p>
            </CardContent>
          </Card>
        )}
      </For>
    </Grid>
  );
}

function MatterDiagnosticsSkeleton() {
  return (
    <Flex
      flexDirection="col"
      alignItems="stretch"
      class="gap-5"
      aria-busy="true"
      aria-live="polite"
    >
      <Grid colsMd={2} class="gap-3">
        <For each={Array.from({ length: 5 })}>
          {() => (
            <Card class="min-h-24">
              <CardContent class="flex h-full flex-col justify-center gap-2 pt-6">
                <Skeleton class="h-3 w-24" />
                <Skeleton class="h-5 w-32" />
              </CardContent>
            </Card>
          )}
        </For>
      </Grid>
      <Grid colsLg={2} class="gap-5">
        <For each={Array.from({ length: 2 })}>
          {(_, index) => (
            <Card>
              <CardHeader>
                <Flex alignItems="start" class="gap-3">
                  <Skeleton class="size-10 shrink-0 rounded-lg" />
                  <Flex
                    flexDirection="col"
                    alignItems="stretch"
                    class="min-w-0 flex-1 gap-2"
                  >
                    <Skeleton class="h-5 w-40" />
                    <Skeleton class="h-4 w-full" />
                    <Skeleton class="h-4 w-4/5" />
                  </Flex>
                </Flex>
              </CardHeader>
              <CardContent>
                <Flex flexDirection="col" alignItems="stretch" class="gap-4">
                  <Skeleton class="h-4 w-24" />
                  <Skeleton class="h-9 w-full" />
                  <Show when={index() === 1}>
                    <Skeleton class="h-4 w-28" />
                    <Skeleton class="h-9 w-full" />
                  </Show>
                  <Skeleton class="h-4 w-3/4" />
                </Flex>
              </CardContent>
              <CardFooter class="justify-end">
                <Skeleton class="h-9 w-36" />
              </CardFooter>
            </Card>
          )}
        </For>
      </Grid>
      <Alert>
        <HiOutlineBeaker />
        <Skeleton class="h-4 w-full max-w-xl" />
      </Alert>
    </Flex>
  );
}

export default function MatterDiagnostics() {
  const matterQuery = createMatterQuery();
  const commissionMutation = matterCommissionMutation();
  const cancelMutation = matterCommissionCancelMutation();
  const matter = () => matterQuery.data ?? fallbackMatter;
  const [commissioningSheet, setCommissioningSheet] =
    createSignal<CommissioningSheet>(null);
  const [submittedOperationId, setSubmittedOperationId] = createSignal<
    number | null
  >(null);
  const wifiForm = createForm<WifiCommissioningValues>({
    defaultValues: { setupCode: "" },
    mode: "onTouched",
  });
  const threadForm = createForm<ThreadCommissioningValues>({
    defaultValues: { setupCode: "" },
    mode: "onTouched",
  });

  const wifiSetupCodeField = wifiForm.register("setupCode", {
    validate: (value) => {
      const result = setupCodeSchema.safeParse(value);
      return result.success || result.error.issues[0]?.message;
    },
  });
  const threadSetupCodeField = threadForm.register("setupCode", {
    validate: (value) => {
      const result = setupCodeSchema.safeParse(value);
      return result.success || result.error.issues[0]?.message;
    },
  });

  const shouldShowToast = createToastGate();

  const closeCommissioningSheet = () => {
    setCommissioningSheet(null);
    wifiForm.reset({ setupCode: "" });
    threadForm.reset({ setupCode: "" });
  };

  const submitWifiCommissioning = wifiForm.handleSubmit((values) => {
    const input = wifiCommissioningSchema.parse(values);
    commissionMutation.mutate(
      {
        transport: "wifi",
        setup_payload: input.setupCode,
      },
      {
        onSuccess: (response) => {
          setSubmittedOperationId(response.operation_id);
          closeCommissioningSheet();
        },
      },
    );
  });

  const submitThreadCommissioning = threadForm.handleSubmit((values) => {
    const input = threadCommissioningSchema.parse(values);
    commissionMutation.mutate(
      {
        transport: "thread",
        setup_payload: input.setupCode,
      },
      {
        onSuccess: (response) => {
          setSubmittedOperationId(response.operation_id);
          closeCommissioningSheet();
        },
      },
    );
  });

  const trackedOperation = () => {
    const snapshot = matter().operation;
    const requested = submittedOperationId();
    return (
      [snapshot.active, snapshot.pending, snapshot.last_terminal].find(
        (operation) => operation?.id === requested,
      ) ??
      snapshot.active ??
      snapshot.pending ??
      snapshot.last_terminal
    );
  };

  createEffect(() => {
    const success = commissionMutation.isSuccess;
    const error = commissionMutation.isError;
    const successMessage =
      commissionMutation.data?.detail ?? "Pairing request received.";
    const errorMessage =
      commissionMutation.error?.message ?? "Matter pairing request failed.";
    const key = success
      ? `success:${successMessage}`
      : error
        ? `error:${errorMessage}`
        : "";
    if (!shouldShowToast(key)) return;
    if (success) toast.success(successMessage);
    if (error) toast.error(errorMessage);
  });

  return (
    <Card class="w-full overflow-hidden">
      <CardHeader>
        <Flex
          alignItems="start"
          justifyContent="between"
          class="flex-col gap-4 sm:flex-row"
        >
          <Flex
            flexDirection="col"
            alignItems="start"
            justifyContent="start"
            class="max-w-3xl gap-2"
          >
            <h1 class="text-xl font-semibold tracking-tight">
              Add Matter Device
            </h1>
            <CardDescription>
              Choose Wi‑Fi or Thread and enter the setup code from your device.
            </CardDescription>
          </Flex>
          <Flex justifyContent="start" class="w-auto shrink-0 flex-wrap gap-2">
            <Button as="a" href="#/devices" variant="outline" size="sm">
              View devices
            </Button>
            <Button as="a" href="#/devices/configure" size="sm">
              Check setup
            </Button>
          </Flex>
        </Flex>
      </CardHeader>

      <CardContent>
        <Flex
          flexDirection="col"
          alignItems="stretch"
          justifyContent="start"
          class="gap-5"
        >
          <Show
            when={!(matterQuery.isFetching && !matterQuery.data)}
            fallback={<MatterDiagnosticsSkeleton />}
          >
            <StatusGrid
              items={
                [
                  {
                    label: "Matter network",
                    value: boolStatus(
                      matter().fabric_configured,
                      "set up",
                      "setup needed",
                    ),
                    tone: okWhen(matter().fabric_configured),
                  },
                  {
                    label: "Wi‑Fi pairing",
                    value: matter().wifi_commissioning_ready
                      ? "ready"
                      : matter().transport_adapter_ready
                        ? "setup needed"
                        : "blocked",
                    tone: matter().wifi_commissioning_ready
                      ? "ok"
                      : matter().transport_adapter_ready
                        ? "muted"
                        : "warn",
                  },
                  {
                    label: "Thread dataset",
                    value: matter().thread_dataset_configured
                      ? "generated and encrypted"
                      : "boot check failed",
                    tone: matter().thread_dataset_configured ? "ok" : "warn",
                  },
                  {
                    label: "Thread pairing",
                    value: matter().thread_commissioning_ready
                      ? "ready"
                      : matter().thread_ready
                        ? "setup needed"
                        : "blocked",
                    tone: matter().thread_commissioning_ready
                      ? "ok"
                      : matter().thread_ready
                        ? "muted"
                        : "warn",
                  },
                  {
                    label: "Discovery",
                    value:
                      matter().mdns_ready && matter().dns_sd_ready
                        ? "ready"
                        : "setup needed",
                    tone:
                      matter().mdns_ready && matter().dns_sd_ready
                        ? "ok"
                        : "muted",
                  },
                ].slice(0, 5) as StatusItem[]
              }
            />
            <Show when={trackedOperation()}>
              {(operation) => {
                const terminal = () =>
                  [
                    "commissioning_complete",
                    "completed",
                    "failed",
                    "cancelled",
                  ].includes(operation().phase);
                const cancellable = () =>
                  ["queued", "discovery", "pase", "attestation"].includes(
                    operation().phase,
                  );
                return (
                  <OperationStatus
                    title={`Matter commissioning: ${operation().phase.replaceAll("_", " ")}`}
                    description={
                      operation().failure?.detail ??
                      `Operation ${operation().id} · ${operation().transport === "wifi" ? "Wi‑Fi" : "Thread"} · retry ${operation().phase_retries} of 3`
                    }
                    active={!terminal()}
                    trailing={
                      <Show when={cancellable()}>
                        <Button
                          variant="outline"
                          size="sm"
                          disabled={cancelMutation.isPending}
                          onClick={() => cancelMutation.mutate(operation().id)}
                        >
                          Cancel
                        </Button>
                      </Show>
                    }
                  />
                );
              }}
            </Show>
            <Accordion collapsible class="rounded-lg border bg-muted/20">
              <AccordionItem value="commissioning-diagnostics" class="border-0">
                <AccordionTrigger class="px-4 py-3 text-left hover:no-underline">
                  <div>
                    <CardTitle class="text-sm">
                      Commissioning diagnostics
                    </CardTitle>
                    <CardDescription class="mt-1 font-normal">
                      Adapter, credential, PASE/CASE, and subscription readiness
                    </CardDescription>
                  </div>
                </AccordionTrigger>
                <AccordionContent class="border-t px-4 pt-3 text-muted-foreground">
                  <StatusGrid
                    items={[
                      {
                        label: "Adapter",
                        value: matter().adapter_status,
                        tone: matter().transport_adapter_ready ? "ok" : "warn",
                      },
                      {
                        label: "Credentials",
                        value: boolStatus(
                          matter().fabric_credentials_ready,
                          "ready",
                          "missing",
                        ),
                        tone: matter().fabric_credentials_ready ? "ok" : "warn",
                      },
                      {
                        label: "PASE / CASE",
                        value:
                          matter().pase_ready && matter().case_ready
                            ? "ready"
                            : "unavailable",
                        tone:
                          matter().pase_ready && matter().case_ready
                            ? "ok"
                            : "warn",
                      },
                      {
                        label: "Subscriptions",
                        value: boolStatus(
                          matter().subscriptions_ready,
                          "ready",
                          "unavailable",
                        ),
                        tone: matter().subscriptions_ready ? "ok" : "muted",
                      },
                    ]}
                  />
                </AccordionContent>
              </AccordionItem>
            </Accordion>

            <Grid colsLg={2} class="gap-5">
              <Card class="flex h-full flex-col">
                <CardHeader>
                  <Flex alignItems="start" justifyContent="start" class="gap-3">
                    <div class="grid size-10 shrink-0 place-items-center rounded-lg border bg-background">
                      <HiOutlineWifi />
                    </div>
                    <div class="min-w-0 flex-1">
                      <CardTitle>Matter over Wi‑Fi</CardTitle>
                      <CardDescription>
                        Enter the manual setup code. Core Pilot validates it
                        with rs-matter now; real Wi‑Fi commissioning remains
                        blocked until the adapter phases below are complete.
                      </CardDescription>
                    </div>
                    <Tooltip>
                      <TooltipTrigger aria-label="Wi-Fi pairing code help">
                        <HiOutlineInformationCircle class="text-muted-foreground" />
                      </TooltipTrigger>
                      <TooltipContent>
                        Use the numeric or manual pairing code from the device.
                      </TooltipContent>
                    </Tooltip>
                  </Flex>
                </CardHeader>
                <CardContent class="flex-1">
                  <p class="text-sm text-muted-foreground">
                    Step 1: confirm Wi‑Fi and discovery readiness. Step 2: enter
                    the setup code. Step 3: submit the bounded request and wait
                    for commissioning status; acceptance alone is not
                    completion.
                  </p>
                </CardContent>
                <CardFooter class="justify-end">
                  <Button
                    type="button"
                    onClick={() => setCommissioningSheet("wifi")}
                  >
                    {matter().wifi_commissioning_ready &&
                    matter().pase_ready &&
                    matter().case_ready
                      ? "Start Wi‑Fi journey"
                      : "Validate Wi‑Fi setup code"}
                  </Button>
                </CardFooter>
              </Card>

              <Card class="flex h-full flex-col">
                <CardHeader>
                  <Flex alignItems="start" justifyContent="start" class="gap-3">
                    <div class="grid size-10 shrink-0 place-items-center rounded-lg border bg-background">
                      <HiOutlineSignal />
                    </div>
                    <div class="min-w-0 flex-1">
                      <CardTitle>Matter over Thread</CardTitle>
                      <CardDescription>
                        Core Pilot creates its Thread network during boot and
                        stores the operational dataset only through encrypted SD
                        persistence. Pair devices using their setup code.
                      </CardDescription>
                    </div>
                    <Tooltip>
                      <TooltipTrigger aria-label="Thread pairing help">
                        <HiOutlineInformationCircle class="text-muted-foreground" />
                      </TooltipTrigger>
                      <TooltipContent>
                        Thread pairing uses the boot-generated encrypted Active
                        Operational Dataset; its credentials are never returned
                        to the browser.
                      </TooltipContent>
                    </Tooltip>
                  </Flex>
                </CardHeader>
                <CardContent class="flex-1">
                  <p class="text-sm text-muted-foreground">
                    Step 1: verify the boot-generated dataset and RCP readiness.
                    Step 2: enter the device setup code. Step 3: submit and wait
                    for actual commissioning; acceptance is not completion.
                  </p>
                </CardContent>
                <CardFooter class="flex-wrap justify-end gap-2">
                  <Button
                    type="button"
                    onClick={() => setCommissioningSheet("thread")}
                  >
                    {matter().thread_commissioning_ready &&
                    matter().pase_ready &&
                    matter().case_ready
                      ? "Start Thread journey"
                      : "Validate Thread setup code"}
                  </Button>
                </CardFooter>
              </Card>
            </Grid>

            <Alert>
              <HiOutlineBeaker />
              <span>{matter().detail}</span>
            </Alert>
          </Show>
        </Flex>
      </CardContent>

      <Sheet
        open={commissioningSheet() === "wifi"}
        onOpenChange={(open) => {
          if (open) setCommissioningSheet("wifi");
          else closeCommissioningSheet();
        }}
      >
        <SheetContent
          position="right"
          class="flex w-full max-w-none flex-col gap-0 overflow-hidden! p-0 sm:max-w-md"
        >
          <SheetHeader class="shrink-0 border-b px-6 py-5 pr-12">
            <SheetTitle>Matter over Wi‑Fi</SheetTitle>
            <SheetDescription>
              Wi‑Fi commissioning uses the setup code from the device. Core
              Pilot keeps this transport separate from Thread network details.
            </SheetDescription>
          </SheetHeader>
          <form
            class="flex min-h-0 flex-1 flex-col overflow-hidden"
            onSubmit={submitWifiCommissioning}
          >
            <div class="min-h-0 flex-1 space-y-5 overflow-y-auto p-6">
              <Stepper
                steps={[
                  { label: "Ready", icon: HiOutlineWifi },
                  { label: "Code", icon: HiOutlineInformationCircle },
                  { label: "Request", icon: HiOutlineCheck },
                ]}
                activeStep={commissionMutation.isSuccess ? 2 : 1}
                maxReachableStep={1}
                onStepChange={() => undefined}
                aria-label="Matter over Wi-Fi journey"
              />
              <Alert>
                Setup-code parsing is available now. Core Pilot will report the
                first missing runtime prerequisite and will not claim the device
                was commissioned until PASE, AddNOC, CASE, and subscriptions
                complete.
              </Alert>
              <TextField
                validationState={
                  wifiForm.errors.setupCode ? "invalid" : "valid"
                }
              >
                <TextFieldLabel for="wifi-setup-code">
                  Setup code
                </TextFieldLabel>
                <TextFieldInput
                  {...wifiSetupCodeField}
                  id="wifi-setup-code"
                  maxlength={512}
                  placeholder="Enter manual setup code"
                  autocomplete="off"
                  aria-invalid={Boolean(wifiForm.errors.setupCode)}
                />
                <TextFieldDescription>
                  Keep the device powered and nearby during pairing.
                </TextFieldDescription>
                <Show when={wifiForm.errors.setupCode?.message}>
                  {(message) => (
                    <TextFieldErrorMessage>{message()}</TextFieldErrorMessage>
                  )}
                </Show>
              </TextField>
              <Show
                when={
                  commissionMutation.isPending ||
                  commissionMutation.isSuccess ||
                  commissionMutation.isError
                }
              >
                <OperationStatus
                  title={
                    commissionMutation.isPending
                      ? "Validating Wi-Fi pairing request"
                      : commissionMutation.isError
                        ? "Pairing request rejected"
                        : "Pairing request accepted"
                  }
                  description={
                    commissionMutation.error?.message ??
                    commissionMutation.data?.detail ??
                    "Core Pilot is validating prerequisites. Acceptance does not imply commissioning completed."
                  }
                  active={commissionMutation.isPending}
                />
              </Show>
            </div>
            <SheetFooter class="shrink-0 gap-2 border-t bg-background px-6 py-4">
              <Button
                type="button"
                variant="outline"
                onClick={closeCommissioningSheet}
              >
                Cancel
              </Button>
              <Button type="submit" disabled={commissionMutation.isPending}>
                <Show when={commissionMutation.isPending}>
                  <HiOutlineArrowPath
                    class="animate-spin"
                    data-icon="inline-start"
                  />
                </Show>
                {commissionMutation.isPending
                  ? "Checking…"
                  : matter().wifi_commissioning_ready
                    ? "Add with Wi‑Fi"
                    : "Validate Wi‑Fi code"}
              </Button>
            </SheetFooter>
          </form>
        </SheetContent>
      </Sheet>

      <Sheet
        open={commissioningSheet() === "thread"}
        onOpenChange={(open) => {
          if (open) setCommissioningSheet("thread");
          else closeCommissioningSheet();
        }}
      >
        <SheetContent
          position="right"
          class="flex w-full max-w-none flex-col gap-0 overflow-hidden! p-0 sm:max-w-md"
        >
          <SheetHeader class="shrink-0 border-b px-6 py-5 pr-12">
            <SheetTitle>Matter over Thread</SheetTitle>
            <SheetDescription>
              Experimental: Thread commissioning uses the encrypted Active
              Operational Dataset already configured on Core Pilot and requires
              a validated Gateway H2 OpenThread RCP attach.
            </SheetDescription>
          </SheetHeader>
          <form
            class="flex min-h-0 flex-1 flex-col overflow-hidden"
            onSubmit={submitThreadCommissioning}
          >
            <div class="min-h-0 flex-1 space-y-5 overflow-y-auto p-6">
              <Stepper
                steps={[
                  { label: "Ready", icon: HiOutlineSignal },
                  { label: "Code", icon: HiOutlineInformationCircle },
                  { label: "Request", icon: HiOutlineCheck },
                ]}
                activeStep={commissionMutation.isSuccess ? 2 : 1}
                maxReachableStep={1}
                onStepChange={() => undefined}
                aria-label="Matter over Thread journey"
              />
              <Alert>
                Core Pilot will use the dataset generated, encrypted, and
                verified during boot. Commissioning remains blocked until the
                Gateway H2 RCP attaches and PASE, AddNOC, network configuration,
                CASE, subscriptions, and persistence complete.
              </Alert>
              <TextField
                validationState={
                  threadForm.errors.setupCode ? "invalid" : "valid"
                }
              >
                <TextFieldLabel for="thread-setup-code">
                  Setup code
                </TextFieldLabel>
                <TextFieldInput
                  {...threadSetupCodeField}
                  id="thread-setup-code"
                  maxlength={512}
                  placeholder="Enter manual setup code"
                  autocomplete="off"
                  aria-invalid={Boolean(threadForm.errors.setupCode)}
                />
                <Show when={threadForm.errors.setupCode?.message}>
                  {(message) => (
                    <TextFieldErrorMessage>{message()}</TextFieldErrorMessage>
                  )}
                </Show>
              </TextField>

              <Show
                when={
                  commissionMutation.isPending ||
                  commissionMutation.isSuccess ||
                  commissionMutation.isError
                }
              >
                <OperationStatus
                  title={
                    commissionMutation.isPending
                      ? "Validating Thread pairing request"
                      : commissionMutation.isError
                        ? "Pairing request rejected"
                        : "Pairing request accepted"
                  }
                  description={
                    commissionMutation.error?.message ??
                    commissionMutation.data?.detail ??
                    "Core Pilot is validating prerequisites. Acceptance does not imply commissioning completed."
                  }
                  active={commissionMutation.isPending}
                />
              </Show>
            </div>
            <SheetFooter class="shrink-0 gap-2 border-t bg-background px-6 py-4">
              <Button
                type="button"
                variant="outline"
                onClick={closeCommissioningSheet}
              >
                Cancel
              </Button>
              <Button type="submit" disabled={commissionMutation.isPending}>
                <Show when={commissionMutation.isPending}>
                  <HiOutlineArrowPath
                    class="animate-spin"
                    data-icon="inline-start"
                  />
                </Show>
                {commissionMutation.isPending
                  ? "Checking…"
                  : matter().thread_commissioning_ready
                    ? "Add with Thread"
                    : "Validate Thread code"}
              </Button>
            </SheetFooter>
          </form>
        </SheetContent>
      </Sheet>
    </Card>
  );
}
