import { For, Show, createSignal, type Component } from "solid-js";
import { createForm } from "solid-hook-form";
import {
  HiOutlineBolt,
  HiOutlineCheckCircle,
  HiOutlinePencilSquare,
  HiOutlineRectangleGroup,
} from "solid-icons/hi";
import { toast } from "solid-sonner";
import { z } from "zod";
import { Badge } from "~/components/badge";
import { AutomationBuilder } from "~/components/automation-builder";
import { Button } from "~/components/button";
import { ConfirmDestructiveAction } from "~/components/confirm-destructive-action";
import { EmptyState } from "~/components/empty-state";
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
import { Label } from "~/components/label";
import { entityLimits } from "~/lib/request-contracts";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "~/components/select";
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
  Switch,
  SwitchControl,
  SwitchLabel,
  SwitchThumb,
} from "~/components/switch";
import {
  TextField,
  TextFieldDescription,
  TextFieldInput,
  TextFieldLabel,
} from "~/components/text-field";

import { ToggleGroup, ToggleGroupItem } from "~/components/toggle-group";
import type { Automation, Device } from "../lib/api";
import {
  applyAutomationTemplate,
  automationTemplates,
  type AutomationTemplateId,
} from "../lib/automation-templates";
import {
  actionSummary,
  automationHasLegacyValues,
  decodeAction,
  decodeTrigger,
  encodeAction,
  encodeTrigger,
  MAX_AUTOMATION_ACTIONS,
  triggerSummary,
  validateAutomationDraft,
  type ActionDraft,
  type TriggerDraft,
} from "../lib/automations";
import {
  automationDeleteMutation,
  automationRunMutation,
  automationUpsertMutation,
} from "../mutations/models";
import {
  automationsQuery as createAutomationsQuery,
  fallbackAutomations,
} from "../queries/automations";
import {
  devicesQuery as createDevicesQuery,
  fallbackDevices,
} from "../queries/devices";
import {
  scenesQuery as createScenesQuery,
  fallbackScenes,
} from "../queries/scenes";

type Choice = { value: string; label: string };

const steps = [
  { label: "Basics", icon: HiOutlinePencilSquare },
  { label: "Trigger", icon: HiOutlineBolt },
  { label: "Actions", icon: HiOutlineRectangleGroup },
  { label: "Review", icon: HiOutlineCheckCircle },
] as const;
const triggerItems: Choice[] = [{ value: "manual", label: "Manual" }];

const automationFormSchema = z
  .object({
    name: z.string(),
    enabled: z.boolean(),
    trigger: z.custom<TriggerDraft>(),
    actions: z.array(z.custom<ActionDraft>()),
    selectedWeekdays: z.array(z.string()),
  })
  .superRefine((values, context) => {
    const error = validateAutomationDraft(
      values.name,
      values.trigger,
      values.actions,
    );
    if (error)
      context.addIssue({ code: "custom", path: ["root"], message: error });
  });

type AutomationFormValues = z.input<typeof automationFormSchema>;

const blankFormValues = (): AutomationFormValues => ({
  name: "",
  enabled: true,
  trigger: { type: "manual" },
  actions: [],
  selectedWeekdays: [],
});

const weekdays = [
  { value: "1", label: "Mon" },
  { value: "2", label: "Tue" },
  { value: "4", label: "Wed" },
  { value: "8", label: "Thu" },
  { value: "16", label: "Fri" },
  { value: "32", label: "Sat" },
  { value: "64", label: "Sun" },
];

const deviceId = (device: Device) =>
  typeof device.id === "string" ? device.id : device.id[0];
const timezoneOffset = () => -new Date().getTimezoneOffset();
const today = () => {
  const date = new Date();
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, "0")}-${String(date.getDate()).padStart(2, "0")}`;
};

function blankAction(type: ActionDraft["type"] = "set_on_off"): ActionDraft {
  if (type === "set_on_off") return { type, deviceId: "", on: true };
  if (type === "set_level") return { type, deviceId: "", level: 50 };
  if (type === "activate_scene") return { type, sceneId: "" };
  if (type === "local_notification") return { type, message: "" };
  if (type === "delay") return { type, milliseconds: 1000 };
  return { type: "legacy", value: "" };
}

const ChoiceSelect: Component<{
  items: Choice[];
  value?: string;
  placeholder: string;
  label: string;
  onChange: (value: string) => void;
}> = (props) => (
  <Select<Choice>
    options={props.items}
    optionValue="value"
    optionTextValue="label"
    value={props.items.find((item) => item.value === props.value) ?? null}
    aria-label={props.label}
    onChange={(item) => item && props.onChange(item.value)}
    itemComponent={(itemProps) => (
      <SelectItem item={itemProps.item}>
        {itemProps.item.rawValue.label}
      </SelectItem>
    )}
  >
    <SelectTrigger>
      <SelectValue<Choice>>
        {(state) => state.selectedOption()?.label ?? props.placeholder}
      </SelectValue>
    </SelectTrigger>
    <SelectContent />
  </Select>
);

export default function Automations() {
  const automations = createAutomationsQuery();
  const devicesQuery = createDevicesQuery();
  const scenesQuery = createScenesQuery();
  const upsertMutation = automationUpsertMutation();
  const runMutation = automationRunMutation();
  const deleteMutation = automationDeleteMutation();

  const response = () => automations.data ?? fallbackAutomations;
  const devices = () => devicesQuery.data?.devices ?? fallbackDevices.devices;
  const scenes = () => scenesQuery.data?.scenes ?? fallbackScenes.scenes;
  const automationsInitialLoading = () =>
    automations.isFetching && !automations.data;
  const devicesInitialLoading = () =>
    devicesQuery.isFetching && !devicesQuery.data;
  const scenesInitialLoading = () =>
    scenesQuery.isFetching && !scenesQuery.data;

  const [step, setStep] = createSignal(0);
  const [editingId, setEditingId] = createSignal<string | null>(null);
  const [isSheetOpen, setIsSheetOpen] = createSignal(false);
  const [validationMessage, setValidationMessage] = createSignal<string | null>(
    null,
  );
  const { handleSubmit, reset, setValue, values } =
    createForm<AutomationFormValues>({
      defaultValues: blankFormValues(),
      mode: "onSubmit",
      resolver: async (formValues: AutomationFormValues) => {
        const result = automationFormSchema.safeParse(formValues);
        if (result.success) return { values: result.data, errors: {} };
        return {
          values: {},
          errors: Object.fromEntries(
            result.error.issues.map((issue) => [
              issue.path.join("."),
              { type: issue.code, message: issue.message },
            ]),
          ),
        };
      },
    });
  const name = () => values().name;
  const enabled = () => values().enabled;
  const trigger = () => values().trigger;
  const actions = () => values().actions;
  const selectedWeekdays = () => values().selectedWeekdays;

  function setTriggerType(type: string) {
    if (type === "once") {
      setValue("trigger", {
        type,
        date: today(),
        time: "09:00",
        timezoneOffsetMinutes: timezoneOffset(),
      });
    } else if (type === "weekly") {
      setValue("trigger", {
        type,
        weekdaysMask: 31,
        time: "09:00",
        timezoneOffsetMinutes: timezoneOffset(),
      });
      setValue("selectedWeekdays", ["1", "2", "4", "8", "16"]);
    } else {
      setValue("trigger", { type: "manual" });
      setValue("selectedWeekdays", []);
    }
  }

  function updateTrigger(patch: Partial<TriggerDraft>) {
    setValue("trigger", { ...trigger(), ...patch } as TriggerDraft);
  }

  function updateAction(index: number, patch: Partial<ActionDraft>) {
    setValue(
      "actions",
      actions().map((action, actionIndex) =>
        actionIndex === index
          ? ({ ...action, ...patch } as ActionDraft)
          : action,
      ),
    );
  }

  function setActionType(index: number, type: string) {
    setValue(
      "actions",
      actions().map((action, actionIndex) =>
        actionIndex === index
          ? blankAction(type as ActionDraft["type"])
          : action,
      ),
    );
  }

  function addAction(type: Exclude<ActionDraft["type"], "legacy">) {
    if (actions().length < MAX_AUTOMATION_ACTIONS)
      setValue("actions", [...actions(), blankAction(type)]);
  }

  function moveAction(from: number, to: number) {
    if (
      from < 0 ||
      from >= actions().length ||
      to < 0 ||
      to >= actions().length
    )
      return;
    const reordered = [...actions()];
    const [moved] = reordered.splice(from, 1);
    reordered.splice(to, 0, moved);
    setValue("actions", reordered);
  }

  function removeAction(index: number) {
    setValue(
      "actions",
      actions().filter((_, actionIndex) => actionIndex !== index),
    );
  }

  function resetForm() {
    setStep(0);
    setEditingId(null);
    reset(blankFormValues());
    setValidationMessage(null);
  }

  function closeSheet() {
    setIsSheetOpen(false);
    resetForm();
  }

  function createAutomation() {
    resetForm();
    setIsSheetOpen(true);
  }

  function useTemplate(templateId: AutomationTemplateId) {
    const draft = applyAutomationTemplate(templateId, timezoneOffset());
    if (!draft) return;
    reset(draft);
    setValidationMessage(null);
  }

  function edit(automation: Automation) {
    const nextTrigger = decodeTrigger(automation.trigger);
    setEditingId(automation.id);
    const mask = nextTrigger.type === "weekly" ? nextTrigger.weekdaysMask : 0;
    reset({
      name: automation.name,
      enabled: automation.enabled,
      trigger: nextTrigger,
      actions: automation.actions.map(decodeAction),
      selectedWeekdays: weekdays
        .filter((day) => (mask & Number(day.value)) !== 0)
        .map((day) => day.value),
    });
    setStep(0);
    setValidationMessage(null);
    setIsSheetOpen(true);
  }

  function selectStep(nextStep: number) {
    if (nextStep <= step()) {
      setValidationMessage(null);
      setStep(nextStep);
    } else if (nextStep === step() + 1) {
      continueWizard();
    }
  }

  function continueWizard() {
    if (step() === 0 && !name().trim()) {
      setValidationMessage("Give this automation a name.");
      return;
    }
    if (step() === 1 && trigger().type === "legacy") {
      setValidationMessage("Choose a supported trigger.");
      return;
    }
    if (step() === 2 && !actions().length) {
      setValidationMessage("Add at least one action.");
      return;
    }
    setValidationMessage(null);
    setStep((current) => Math.min(3, current + 1));
  }

  const submitAutomation = handleSubmit(
    (formValues) => {
      setValidationMessage(null);
      upsertMutation.mutate(
        {
          id: editingId(),
          name: formValues.name.trim(),
          enabled: formValues.enabled,
          trigger: encodeTrigger(formValues.trigger),
          actions: formValues.actions.map(encodeAction),
        },
        {
          onSuccess: (result) => {
            if (result.accepted) closeSheet();
            toast.success(result.detail ?? "Automation saved.");
          },
          onError: (error) => toast.error(error.message),
        },
      );
    },
    () => {
      const error = validateAutomationDraft(name(), trigger(), actions());
      setValidationMessage(error);
      if (error) toast.error(error);
    },
  );

  function updateWeekdays(nextWeekdays: string[]) {
    setValue("selectedWeekdays", nextWeekdays);
    const mask = nextWeekdays.reduce(
      (total, value) => total | Number(value),
      0,
    );
    updateTrigger({ weekdaysMask: mask });
  }

  return (
    <Card class="w-full overflow-hidden">
      <CardHeader>
        <Flex
          alignItems="start"
          justifyContent="between"
          class="flex-col gap-4 sm:flex-row"
        >
          <div class="max-w-3xl">
            <div class="mb-3 h-px w-12 bg-foreground" />
            <h1 class="text-xl font-semibold tracking-tight">Automations</h1>
            <CardDescription>
              Create bounded local routines that work without the assistant.
            </CardDescription>
          </div>
          <Flex class="w-full flex-wrap gap-2 sm:w-auto" justifyContent="start">
            <Button as="a" size="sm" variant="outline" href="#/devices">
              View devices
            </Button>
            <Button type="button" size="sm" onClick={createAutomation}>
              Create automation
            </Button>
          </Flex>
        </Flex>
      </CardHeader>
      <CardContent>
        <Flex
          flexDirection="col"
          alignItems="stretch"
          justifyContent="start"
          class="gap-6"
        >
          <Sheet
            open={isSheetOpen()}
            onOpenChange={(open) =>
              open ? setIsSheetOpen(true) : closeSheet()
            }
          >
            <SheetContent class="flex w-full flex-col gap-0 p-0 sm:max-w-2xl lg:max-w-3xl">
              <SheetHeader class="shrink-0 border-b px-6 py-5 pr-12">
                <SheetTitle>
                  {editingId() ? "Edit automation" : "Create automation"}
                </SheetTitle>
                <SheetDescription>
                  Create a trigger, then chain up to eight deterministic local
                  actions.
                </SheetDescription>
              </SheetHeader>
              <form
                class="flex min-h-0 flex-1 flex-col"
                onSubmit={submitAutomation}
              >
                <div class="min-h-0 flex-1 space-y-6 overflow-y-auto px-6 py-5">
                  <Stepper
                    steps={steps}
                    activeStep={step()}
                    maxReachableStep={Math.min(steps.length - 1, step() + 1)}
                    onStepChange={selectStep}
                    aria-label="Automation steps"
                  />

                  <Show when={step() === 0}>
                    <Card>
                      <CardHeader>
                        <CardTitle>Basics</CardTitle>
                        <CardDescription>
                          Name the routine and choose whether it starts enabled.
                        </CardDescription>
                      </CardHeader>
                      <CardContent class="flex flex-col gap-5">
                        <Show when={!editingId()}>
                          <div class="space-y-3">
                            <div>
                              <h3 class="text-sm font-medium">
                                Start from a template
                              </h3>
                              <p class="text-xs text-muted-foreground">
                                Templates only prefill this editable draft.
                                Nothing is saved until you review and create it.
                              </p>
                            </div>
                            <Grid cols={1} colsSm={2} class="gap-2">
                              <For each={automationTemplates}>
                                {(template) => (
                                  <button
                                    type="button"
                                    disabled={!template.available}
                                    class="min-h-24 rounded-lg border bg-background p-3 text-left transition-colors hover:border-primary/50 hover:bg-accent focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:cursor-not-allowed disabled:opacity-60"
                                    onClick={() => useTemplate(template.id)}
                                  >
                                    <Flex class="flex-col items-start gap-2 sm:flex-row">
                                      <div class="min-w-0 flex-1">
                                        <p class="text-sm font-medium">
                                          {template.title}
                                        </p>
                                        <p class="mt-1 text-xs leading-relaxed text-muted-foreground">
                                          {template.description}
                                        </p>
                                      </div>
                                      <Badge
                                        variant={
                                          template.available
                                            ? "secondary"
                                            : "outline"
                                        }
                                      >
                                        {template.available
                                          ? "Available"
                                          : "Unavailable"}
                                      </Badge>
                                    </Flex>
                                    <Show
                                      when={
                                        !template.available &&
                                        template.prerequisite
                                      }
                                    >
                                      {(prerequisite) => (
                                        <p class="mt-2 text-xs text-muted-foreground">
                                          {prerequisite()}
                                        </p>
                                      )}
                                    </Show>
                                  </button>
                                )}
                              </For>
                            </Grid>
                          </div>
                        </Show>
                        <TextField
                          value={name()}
                          onChange={(value) => setValue("name", value)}
                          validationState={
                            validationMessage() && !name().trim()
                              ? "invalid"
                              : "valid"
                          }
                        >
                          <TextFieldLabel>Name</TextFieldLabel>
                          <TextFieldInput
                            id="automation-name"
                            maxlength={entityLimits.name}
                            placeholder="Wind-down routine"
                          />
                          <TextFieldDescription>
                            Shown in the dashboard and local audit history.
                          </TextFieldDescription>
                        </TextField>
                        <Switch
                          checked={enabled()}
                          onChange={(value) => setValue("enabled", value)}
                          class="flex items-start gap-3 rounded-md border p-3"
                        >
                          <SwitchControl class="mt-0.5">
                            <SwitchThumb />
                          </SwitchControl>
                          <Flex
                            flexDirection="col"
                            alignItems="start"
                            justifyContent="start"
                            class="gap-1"
                          >
                            <SwitchLabel>Enable after saving</SwitchLabel>
                            <p class="text-sm text-muted-foreground">
                              Disabled routines stay saved without reacting to
                              triggers.
                            </p>
                          </Flex>
                        </Switch>
                      </CardContent>
                    </Card>
                  </Show>

                  <Show when={step() === 1}>
                    <Card>
                      <CardHeader>
                        <CardTitle>Choose a trigger</CardTitle>
                        <CardDescription>
                          Schedules use this browser’s current UTC offset and
                          run locally.
                        </CardDescription>
                      </CardHeader>
                      <CardContent class="flex flex-col gap-5">
                        <div class="flex flex-col gap-2">
                          <Label>Trigger type</Label>
                          <ChoiceSelect
                            items={triggerItems}
                            value={
                              trigger().type === "legacy"
                                ? undefined
                                : trigger().type
                            }
                            placeholder="Select a trigger"
                            label="Trigger type"
                            onChange={setTriggerType}
                          />
                        </div>
                        <Show
                          when={
                            trigger().type === "once" &&
                            (trigger() as Extract<
                              TriggerDraft,
                              { type: "once" }
                            >)
                          }
                          keyed
                        >
                          {(once) => (
                            <Grid cols={1} colsSm={2} class="min-w-0 gap-2">
                              <TextField
                                value={once.date}
                                onChange={(date) => updateTrigger({ date })}
                              >
                                <TextFieldLabel>Date</TextFieldLabel>
                                <TextFieldInput type="date" />
                              </TextField>
                              <TextField
                                value={once.time}
                                onChange={(time) => updateTrigger({ time })}
                              >
                                <TextFieldLabel>Time</TextFieldLabel>
                                <TextFieldInput id="once-time" type="time" />
                              </TextField>
                            </Grid>
                          )}
                        </Show>
                        <Show
                          when={
                            trigger().type === "weekly" &&
                            (trigger() as Extract<
                              TriggerDraft,
                              { type: "weekly" }
                            >)
                          }
                          keyed
                        >
                          {(weekly) => (
                            <div class="flex flex-col gap-5">
                              <fieldset class="flex flex-col gap-2">
                                <legend class="text-sm font-medium">
                                  Weekdays
                                </legend>
                                <ToggleGroup
                                  multiple
                                  value={selectedWeekdays()}
                                  onChange={updateWeekdays}
                                  class="flex-wrap justify-start"
                                >
                                  <For each={weekdays}>
                                    {(day) => (
                                      <ToggleGroupItem
                                        value={day.value}
                                        aria-label={day.label}
                                      >
                                        {day.label}
                                      </ToggleGroupItem>
                                    )}
                                  </For>
                                </ToggleGroup>
                              </fieldset>
                              <TextField
                                value={weekly.time}
                                onChange={(time) => updateTrigger({ time })}
                              >
                                <TextFieldLabel>Time</TextFieldLabel>
                                <TextFieldInput id="weekly-time" type="time" />
                              </TextField>
                            </div>
                          )}
                        </Show>
                        <Show when={trigger().type === "legacy"}>
                          <p class="text-sm text-destructive">
                            This saved trigger uses an older free-form value.
                            Select a supported trigger to continue.
                          </p>
                        </Show>
                      </CardContent>
                    </Card>
                  </Show>

                  <Show when={step() === 2}>
                    <AutomationBuilder
                      triggerSummary={triggerSummary(trigger())}
                      actions={actions()}
                      devices={devices().map((device) => ({
                        value: deviceId(device),
                        label: device.name,
                      }))}
                      scenes={scenes().map((scene) => ({
                        value: scene.id,
                        label: scene.name,
                      }))}
                      devicesLoading={devicesInitialLoading()}
                      scenesLoading={scenesInitialLoading()}
                      onAdd={addAction}
                      onChangeType={(index, type) => setActionType(index, type)}
                      onUpdate={updateAction}
                      onMove={moveAction}
                      onRemove={removeAction}
                    />
                  </Show>

                  <Show when={step() === 3}>
                    <Card>
                      <CardHeader>
                        <CardTitle>Review and save</CardTitle>
                        <CardDescription>
                          Core Pilot validates every bounded step before
                          encrypted persistence.
                        </CardDescription>
                      </CardHeader>
                      <CardContent class="flex flex-col gap-4">
                        <div>
                          <p class="text-sm font-medium">{name()}</p>
                          <p class="text-sm text-muted-foreground">
                            {enabled() ? "Enabled" : "Disabled"} ·{" "}
                            {triggerSummary(trigger())}
                          </p>
                        </div>
                        <ol class="flex flex-col gap-2">
                          <For each={actions()}>
                            {(action, index) => (
                              <li class="flex items-center gap-3 rounded-md border p-3">
                                <Badge variant="outline">{index() + 1}</Badge>
                                <span class="min-w-0 wrap-anywhere text-sm">
                                  {actionSummary(action)}
                                </span>
                              </li>
                            )}
                          </For>
                        </ol>
                      </CardContent>
                    </Card>
                  </Show>

                  <Show when={validationMessage()}>
                    <p class="text-sm text-destructive" role="alert">
                      {validationMessage()}
                    </p>
                  </Show>
                </div>
                <SheetFooter class="shrink-0 gap-2 border-t bg-background px-6 py-4">
                  <Button
                    type="button"
                    size="sm"
                    variant="ghost"
                    onClick={closeSheet}
                  >
                    Cancel
                  </Button>
                  <Flex class="w-full flex-col-reverse gap-2 sm:w-auto sm:flex-row [&>button]:w-full sm:[&>button]:w-auto">
                    <Show when={step() > 0}>
                      <Button
                        type="button"
                        size="sm"
                        variant="outline"
                        onClick={() => {
                          setValidationMessage(null);
                          setStep((current) => current - 1);
                        }}
                      >
                        Back
                      </Button>
                    </Show>
                    <Show
                      when={step() < 3}
                      fallback={
                        <Button
                          type="submit"
                          size="sm"
                          disabled={upsertMutation.isPending}
                        >
                          {editingId()
                            ? "Update automation"
                            : "Create automation"}
                        </Button>
                      }
                    >
                      <Button type="button" size="sm" onClick={continueWizard}>
                        Continue
                      </Button>
                    </Show>
                  </Flex>
                </SheetFooter>
              </form>
            </SheetContent>
          </Sheet>

          <Show
            when={
              runMutation.isPending ||
              runMutation.isSuccess ||
              runMutation.isError
            }
          >
            <OperationStatus
              title={
                runMutation.isPending
                  ? "Requesting automation run"
                  : runMutation.isError
                    ? "Automation run was not queued"
                    : "Automation run queued"
              }
              description={
                runMutation.error?.message ??
                runMutation.data?.detail ??
                "The request is entering the bounded local automation queue."
              }
              active={runMutation.isPending}
            />
          </Show>

          <Flex
            alignItems="start"
            class="flex-col gap-4 sm:flex-row sm:items-end"
          >
            <div class="min-w-0">
              <h2 class="text-xl font-semibold">Saved routines</h2>
              <Show
                when={!automationsInitialLoading()}
                fallback={<Skeleton class="mt-1 h-4 w-44" />}
              >
                <p class="text-sm text-muted-foreground">
                  {response().automations.length} stored on encrypted SD
                </p>
              </Show>
            </div>
            <Badge variant="outline">Up to 8 actions each</Badge>
          </Flex>
          <Show
            when={!automationsInitialLoading()}
            fallback={
              <Grid
                cols={1}
                colsMd={2}
                class="gap-4"
                aria-busy="true"
                aria-live="polite"
              >
                <For each={Array.from({ length: 2 })}>
                  {(_, index) => (
                    <Card>
                      <CardHeader class="gap-2">
                        <Flex class="flex-col items-start gap-3 sm:flex-row">
                          <Skeleton
                            class={index() === 0 ? "h-5 w-40" : "h-5 w-52"}
                          />
                          <Skeleton class="h-5 w-16 rounded-full" />
                        </Flex>
                        <Skeleton class="h-4 w-48 max-w-full" />
                      </CardHeader>
                      <CardContent class="space-y-2">
                        <Skeleton class="h-4 w-full" />
                        <Skeleton class="h-4 w-4/5" />
                      </CardContent>
                      <CardFooter class="justify-end gap-2">
                        <Skeleton class="h-9 w-16" />
                        <Skeleton class="h-9 w-14" />
                        <Skeleton class="h-9 w-16" />
                      </CardFooter>
                    </Card>
                  )}
                </For>
              </Grid>
            }
          >
            <Show
              when={response().automations.length}
              fallback={
                <EmptyState
                  title="Build your first local automation"
                  description="Start blank or choose a template. Available drafts use only triggers and actions accepted by firmware."
                  action={
                    <Button type="button" size="sm" onClick={createAutomation}>
                      Browse templates
                    </Button>
                  }
                />
              }
            >
              <Grid cols={1} colsMd={2} class="gap-4">
                <For each={response().automations}>
                  {(automation) => (
                    <Card>
                      <CardHeader>
                        <Flex class="flex-col items-start gap-3 sm:flex-row">
                          <CardTitle class="min-w-0 wrap-anywhere">
                            {automation.name}
                          </CardTitle>
                          <Badge
                            variant={
                              automation.enabled ? "default" : "secondary"
                            }
                          >
                            {automation.enabled ? "Enabled" : "Disabled"}
                          </Badge>
                        </Flex>
                        <CardDescription class="wrap-anywhere">
                          {triggerSummary(decodeTrigger(automation.trigger))}
                        </CardDescription>
                      </CardHeader>
                      <CardContent>
                        <ol class="flex flex-col gap-2">
                          <For each={automation.actions}>
                            {(action, index) => (
                              <li class="flex min-w-0 items-start gap-2 text-sm">
                                <span class="shrink-0 font-mono text-muted-foreground">
                                  {index() + 1}.
                                </span>
                                <span class="min-w-0 wrap-anywhere">
                                  {actionSummary(decodeAction(action))}
                                </span>
                              </li>
                            )}
                          </For>
                        </ol>
                        <Show when={automationHasLegacyValues(automation)}>
                          <p class="mt-3 text-sm text-destructive">
                            Legacy values need review before this automation can
                            be updated.
                          </p>
                        </Show>
                      </CardContent>
                      <CardFooter class="justify-end gap-2">
                        <Show
                          when={
                            automation.enabled &&
                            decodeTrigger(automation.trigger).type === "manual"
                          }
                        >
                          <Button
                            type="button"
                            size="sm"
                            disabled={runMutation.isPending}
                            onClick={() =>
                              runMutation.mutate(
                                { id: automation.id },
                                {
                                  onSuccess: (result) =>
                                    result.accepted
                                      ? toast.success(
                                          result.detail ?? "Automation queued.",
                                        )
                                      : toast.error(
                                          result.detail ??
                                            "Automation could not be queued.",
                                        ),
                                  onError: (error) =>
                                    toast.error(error.message),
                                },
                              )
                            }
                          >
                            Run now
                          </Button>
                        </Show>
                        <Button
                          type="button"
                          size="sm"
                          variant="outline"
                          onClick={() => edit(automation)}
                        >
                          Edit
                        </Button>
                        <ConfirmDestructiveAction
                          triggerLabel="Delete"
                          title={`Delete ${automation.name}?`}
                          description="This permanently removes the trigger and action chain. Devices, scenes, and other automations are not changed."
                          confirmLabel="Delete automation"
                          disabled={deleteMutation.isPending}
                          onConfirm={async () => {
                            const result = await deleteMutation.mutateAsync({
                              id: automation.id,
                            });
                            toast.success(
                              result.detail ?? "Automation deleted.",
                            );
                          }}
                          onError={(error) =>
                            toast.error(
                              error instanceof Error
                                ? error.message
                                : "Automation deletion failed.",
                            )
                          }
                        />
                      </CardFooter>
                    </Card>
                  )}
                </For>
              </Grid>
            </Show>
          </Show>
        </Flex>
      </CardContent>
    </Card>
  );
}
