import { For, Show, createSignal, type Component, type JSX } from "solid-js";
import {
  HiOutlineBolt,
  HiOutlineClock,
  HiOutlineInformationCircle,
  HiOutlinePlus,
  HiOutlineRectangleGroup,
  HiOutlineTrash,
} from "solid-icons/hi";

import { Badge } from "~/components/badge";
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
import { Label } from "~/components/label";
import { entityLimits } from "~/lib/request-contracts";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "~/components/select";
import { Skeleton } from "~/components/skeleton";
import {
  Switch,
  SwitchControl,
  SwitchLabel,
  SwitchThumb,
} from "~/components/switch";

import {
  TextField,
  TextFieldInput,
  TextFieldLabel,
} from "~/components/text-field";
import { Tooltip, TooltipContent, TooltipTrigger } from "~/components/tooltip";
import {
  actionSummary,
  MAX_AUTOMATION_ACTIONS,
  type ActionDraft,
} from "~/lib/automations";
import { cn } from "~/lib/utils";

type Choice = { value: string; label: string };
type ActionType = Exclude<ActionDraft["type"], "legacy">;

type ActionDefinition = {
  type: ActionType;
  title: string;
  description: string;
  icon: typeof HiOutlineBolt;
};

const actionDefinitions: ActionDefinition[] = [
  {
    type: "set_on_off",
    title: "Device power",
    description: "Turn a selected device on or off.",
    icon: HiOutlineBolt,
  },
  {
    type: "set_level",
    title: "Device level",
    description: "Set a device to a level from 0 to 100%.",
    icon: HiOutlineBolt,
  },
  {
    type: "activate_scene",
    title: "Activate scene",
    description: "Run a saved local scene.",
    icon: HiOutlineRectangleGroup,
  },
  {
    type: "local_notification",
    title: "Local notification",
    description: "Create a bounded local notification.",
    icon: HiOutlineBolt,
  },
  {
    type: "delay",
    title: "Wait",
    description: "Pause this chain for up to 60 seconds.",
    icon: HiOutlineClock,
  },
];

const runtimeAutomationActionTypes = new Set<ActionType>([
  "set_on_off",
  "delay",
]);

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
    placeholder={props.placeholder}
    aria-label={props.label}
    onChange={(item) => item && props.onChange(item.value)}
    itemComponent={(itemProps) => (
      <SelectItem item={itemProps.item}>
        {itemProps.item.rawValue.label}
      </SelectItem>
    )}
  >
    <SelectTrigger class="w-full">
      <SelectValue<Choice>>
        {(state) => state.selectedOption()?.label ?? props.placeholder}
      </SelectValue>
    </SelectTrigger>
    <SelectContent />
  </Select>
);

export type AutomationBuilderProps = {
  kind?: "automation" | "scene";
  triggerSummary?: string;
  actions: ActionDraft[];
  devices: Choice[];
  scenes: Choice[];
  devicesLoading: boolean;
  scenesLoading: boolean;
  onAdd: (type: ActionType) => void;
  onChangeType: (index: number, type: ActionType) => void;
  onUpdate: (index: number, patch: Partial<ActionDraft>) => void;
  onMove: (from: number, to: number) => void;
  onRemove: (index: number) => void;
};

export function AutomationBuilder(props: AutomationBuilderProps) {
  const isScene = () => props.kind === "scene";
  const availableDefinitions = () =>
    isScene()
      ? actionDefinitions
      : actionDefinitions.filter((definition) =>
          runtimeAutomationActionTypes.has(definition.type),
        );
  const actionChoices = () =>
    availableDefinitions().map(({ type, title }) => ({
      value: type,
      label: title,
    }));
  const [draggedAction, setDraggedAction] = createSignal<number | null>(null);
  const [dropTarget, setDropTarget] = createSignal<number | null>(null);

  const canAdd = () => props.actions.length < MAX_AUTOMATION_ACTIONS;

  function beginPaletteDrag(event: DragEvent, type: ActionType) {
    event.dataTransfer?.setData("application/x-core-pilot-action", type);
    if (event.dataTransfer) event.dataTransfer.effectAllowed = "copy";
  }

  function beginActionDrag(event: DragEvent, index: number) {
    setDraggedAction(index);
    event.dataTransfer?.setData(
      "application/x-core-pilot-action-index",
      String(index),
    );
    if (event.dataTransfer) event.dataTransfer.effectAllowed = "move";
  }

  function allowDrop(event: DragEvent, index: number) {
    event.preventDefault();
    setDropTarget(index);
    if (event.dataTransfer)
      event.dataTransfer.dropEffect =
        draggedAction() === null ? "copy" : "move";
  }

  function finishDrop(event: DragEvent, index: number) {
    event.preventDefault();
    const paletteType = event.dataTransfer?.getData(
      "application/x-core-pilot-action",
    ) as ActionType | undefined;
    const sourceValue = event.dataTransfer?.getData(
      "application/x-core-pilot-action-index",
    );
    const source = sourceValue ? Number(sourceValue) : draggedAction();

    if (
      paletteType &&
      availableDefinitions().some((item) => item.type === paletteType)
    ) {
      if (canAdd()) props.onAdd(paletteType);
    } else if (source !== null && source !== undefined && source !== index) {
      props.onMove(source, index);
    }
    setDraggedAction(null);
    setDropTarget(null);
  }

  function endDrag() {
    setDraggedAction(null);
    setDropTarget(null);
  }

  return (
    <Grid cols={1} colsLg={3} class="min-w-0 gap-4 lg:items-start">
      <Card class="min-w-0 lg:sticky lg:top-0">
        <CardHeader>
          <Flex justifyContent="start" class="gap-2">
            <CardTitle>Building blocks</CardTitle>
            <Tooltip openDelay={0}>
              <TooltipTrigger
                type="button"
                aria-label="Automation limits"
                class="grid size-7 shrink-0 place-items-center rounded-full text-muted-foreground transition-colors hover:bg-accent hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
              >
                <HiOutlineInformationCircle class="size-4" aria-hidden="true" />
              </TooltipTrigger>
              <TooltipContent class="max-w-64">
                {isScene()
                  ? "Up to eight ordered actions. Firmware validates and encrypts the saved scene."
                  : "One trigger and up to eight ordered actions. Firmware validates and encrypts the saved automation."}
              </TooltipContent>
            </Tooltip>
          </Flex>
          <CardDescription>
            Drag an action into the flow, or use Add on touch and keyboard
            devices.
          </CardDescription>
        </CardHeader>
        <CardContent>
          <div class="grid grid-cols-1 gap-2 sm:grid-cols-2 lg:grid-cols-1">
            <For each={availableDefinitions()}>
              {(definition) => (
                <button
                  type="button"
                  draggable={canAdd()}
                  disabled={!canAdd()}
                  class="group flex min-w-0 items-start gap-3 rounded-lg border bg-background p-3 text-left transition-colors hover:border-primary/50 hover:bg-accent focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:cursor-not-allowed disabled:opacity-50"
                  onDragStart={(event) =>
                    beginPaletteDrag(event, definition.type)
                  }
                  onDragEnd={endDrag}
                  onClick={() => props.onAdd(definition.type)}
                  aria-label={`Add ${definition.title}`}
                >
                  <span class="grid size-9 shrink-0 place-items-center rounded-md bg-primary/10 text-primary">
                    <definition.icon class="size-5" aria-hidden="true" />
                  </span>
                  <span class="min-w-0 flex-1">
                    <span class="block text-sm font-medium">
                      {definition.title}
                    </span>
                    <span class="mt-0.5 block text-xs leading-relaxed text-muted-foreground">
                      {definition.description}
                    </span>
                  </span>
                  <HiOutlinePlus
                    class="mt-2 size-4 shrink-0 text-muted-foreground group-hover:text-foreground"
                    aria-hidden="true"
                  />
                </button>
              )}
            </For>
          </div>
        </CardContent>
      </Card>

      <section
        class="min-w-0 lg:col-span-2"
        aria-labelledby="automation-flow-title"
      >
        <Flex class="mb-4 flex-col items-start gap-3 sm:flex-row sm:items-center">
          <div class="min-w-0 flex-1">
            <h2 id="automation-flow-title" class="text-lg font-semibold">
              {isScene() ? "Scene flow" : "Automation flow"}
            </h2>
            <p class="text-sm text-muted-foreground">
              Actions execute from top to bottom. Drag cards to reorder on
              pointer devices.
            </p>
          </div>
          <Badge variant="outline">
            {props.actions.length} / {MAX_AUTOMATION_ACTIONS} actions
          </Badge>
        </Flex>

        <div class="relative min-w-0">
          <Show when={!isScene()}>
            <Card class="relative border-primary/35 bg-primary/5">
              <CardHeader class="flex-row items-start gap-3 space-y-0">
                <span class="grid size-10 shrink-0 place-items-center rounded-full bg-primary text-primary-foreground">
                  <HiOutlineBolt class="size-5" aria-hidden="true" />
                </span>
                <div class="min-w-0 flex-1">
                  <Badge variant="outline" class="mb-2">
                    Trigger
                  </Badge>
                  <CardTitle class="text-base">When this happens</CardTitle>
                  <CardDescription class="mt-1 wrap-anywhere">
                    {props.triggerSummary}
                  </CardDescription>
                </div>
              </CardHeader>
            </Card>
          </Show>

          <Show
            when={props.actions.length > 0}
            fallback={
              <>
                <Show when={!isScene()}>
                  <FlowConnector />
                </Show>
                <div
                  class="relative rounded-lg border-2 border-dashed p-6 text-center transition-colors"
                  onDragOver={(event) => allowDrop(event, 0)}
                  onDragLeave={() => setDropTarget(null)}
                  onDrop={(event) => finishDrop(event, 0)}
                >
                  <p class="font-medium">Drop the first action here</p>
                  <p class="mt-1 text-sm text-muted-foreground">
                    Or choose a building block using its Add button.
                  </p>
                </div>
              </>
            }
          >
            <ol
              class="relative flex min-w-0 flex-col"
              aria-label="Ordered actions"
            >
              <For each={props.actions}>
                {(action, index) => (
                  <li
                    class={cn(
                      "relative min-w-0 rounded-lg transition-all",
                      dropTarget() === index() &&
                        "ring-2 ring-primary ring-offset-2",
                      draggedAction() === index() && "opacity-50",
                    )}
                    onDragOver={(event) => allowDrop(event, index())}
                    onDragLeave={() => setDropTarget(null)}
                    onDrop={(event) => finishDrop(event, index())}
                  >
                    <Show when={!isScene() || index() > 0}>
                      <FlowConnector />
                    </Show>
                    <ActionCard
                      action={action}
                      index={index()}
                      count={props.actions.length}
                      devices={props.devices}
                      scenes={props.scenes}
                      devicesLoading={props.devicesLoading}
                      scenesLoading={props.scenesLoading}
                      onDragStart={(event) => beginActionDrag(event, index())}
                      onDragEnd={endDrag}
                      onChangeType={(type) => props.onChangeType(index(), type)}
                      onUpdate={(patch) => props.onUpdate(index(), patch)}
                      onMoveUp={() => props.onMove(index(), index() - 1)}
                      onMoveDown={() => props.onMove(index(), index() + 1)}
                      actionChoices={actionChoices()}
                      previousLabel={
                        isScene()
                          ? index() === 0
                            ? "scene start"
                            : `action ${index()}`
                          : index() === 0
                            ? "the trigger"
                            : `action ${index()}`
                      }
                      onRemove={() => props.onRemove(index())}
                    />
                  </li>
                )}
              </For>
            </ol>
          </Show>
        </div>
      </section>
    </Grid>
  );
}

function FlowConnector() {
  return (
    <div
      class="pointer-events-none mx-auto h-4 w-px bg-border"
      aria-hidden="true"
    />
  );
}

type ActionCardProps = {
  action: ActionDraft;
  index: number;
  count: number;
  devices: Choice[];
  scenes: Choice[];
  devicesLoading: boolean;
  scenesLoading: boolean;
  onDragStart: (event: DragEvent) => void;
  onDragEnd: () => void;
  onChangeType: (type: ActionType) => void;
  onUpdate: (patch: Partial<ActionDraft>) => void;
  onMoveUp: () => void;
  onMoveDown: () => void;
  actionChoices: Choice[];
  previousLabel: string;
  onRemove: () => void;
};

function ActionCard(props: ActionCardProps) {
  return (
    <Card class="relative min-w-0 bg-background">
      <CardHeader class="flex-row items-start gap-3 space-y-0">
        <button
          type="button"
          draggable
          onDragStart={props.onDragStart}
          onDragEnd={props.onDragEnd}
          class="mt-0.5 grid size-10 shrink-0 cursor-grab place-items-center rounded-md border bg-muted text-muted-foreground active:cursor-grabbing"
          aria-label={`Drag action ${props.index + 1} to reorder`}
          title="Drag to reorder"
        >
          <span class="font-mono text-sm">{props.index + 1}</span>
        </button>
        <div class="min-w-0 flex-1">
          <Flex class="flex-col items-start gap-2 sm:flex-row">
            <div class="min-w-0 flex-1">
              <CardTitle class="text-base">Action {props.index + 1}</CardTitle>
              <CardDescription class="mt-1 wrap-anywhere">
                {actionSummary(props.action)}
              </CardDescription>
            </div>
            <Badge variant="secondary" class="shrink-0">
              Step {props.index + 2}
            </Badge>
          </Flex>
        </div>
      </CardHeader>
      <CardContent class="grid min-w-0 grid-cols-1 gap-4 sm:grid-cols-2">
        <div class="flex min-w-0 flex-col gap-2 sm:col-span-2">
          <Label>Action type</Label>
          <ChoiceSelect
            items={props.actionChoices}
            value={
              props.action.type === "legacy" ? undefined : props.action.type
            }
            placeholder="Select action"
            label="Action type"
            onChange={(value) => props.onChangeType(value as ActionType)}
          />
        </div>
        <ActionFields {...props} />
      </CardContent>
      <CardFooter class="flex-col items-stretch justify-between gap-3 border-t pt-4 sm:flex-row sm:items-center sm:pt-4">
        <p class="text-xs text-muted-foreground">
          Runs after {props.previousLabel}.
        </p>
        <div class="grid grid-cols-3 gap-2 sm:flex">
          <Button
            type="button"
            size="sm"
            variant="outline"
            onClick={props.onMoveUp}
            disabled={props.index === 0}
            aria-label={`Move action ${props.index + 1} up`}
          >
            ↑ <span class="sr-only sm:not-sr-only">Up</span>
          </Button>
          <Button
            type="button"
            size="sm"
            variant="outline"
            onClick={props.onMoveDown}
            disabled={props.index === props.count - 1}
            aria-label={`Move action ${props.index + 1} down`}
          >
            ↓ <span class="sr-only sm:not-sr-only">Down</span>
          </Button>
          <Button
            type="button"
            size="icon"
            variant="ghost"
            class="text-destructive hover:text-destructive"
            onClick={props.onRemove}
            aria-label={`Remove action ${props.index + 1}`}
            title={`Remove action ${props.index + 1}`}
          >
            <HiOutlineTrash aria-hidden="true" />
          </Button>
        </div>
      </CardFooter>
    </Card>
  );
}

function ActionFields(props: ActionCardProps): JSX.Element {
  const action = () => props.action;
  return (
    <>
      <Show
        when={action().type === "set_on_off" || action().type === "set_level"}
      >
        <div class="flex min-w-0 flex-col gap-2 sm:col-span-2">
          <Label>Device</Label>
          <Show
            when={!props.devicesLoading}
            fallback={<Skeleton class="h-10 w-full" />}
          >
            <ChoiceSelect
              items={props.devices}
              value={
                (action() as Extract<ActionDraft, { deviceId: string }>)
                  .deviceId
              }
              placeholder="Select device"
              label="Device"
              onChange={(deviceId) => props.onUpdate({ deviceId })}
            />
          </Show>
        </div>
      </Show>
      <Show when={action().type === "set_on_off"}>
        <Switch
          checked={
            (action() as Extract<ActionDraft, { type: "set_on_off" }>).on
          }
          onChange={(on) => props.onUpdate({ on })}
          class="flex items-center gap-3 rounded-md border p-3 sm:col-span-2"
        >
          <SwitchControl>
            <SwitchThumb />
          </SwitchControl>
          <SwitchLabel>Turn device on</SwitchLabel>
        </Switch>
      </Show>
      <Show when={action().type === "set_level"}>
        <TextField
          value={String(
            (action() as Extract<ActionDraft, { type: "set_level" }>).level,
          )}
          onChange={(value) => props.onUpdate({ level: Number(value) })}
          class="sm:col-span-2"
        >
          <TextFieldLabel>Level (%)</TextFieldLabel>
          <TextFieldInput
            id={`level-${props.index}`}
            type="number"
            min="0"
            max="100"
          />
        </TextField>
      </Show>
      <Show when={action().type === "activate_scene"}>
        <div class="flex min-w-0 flex-col gap-2 sm:col-span-2">
          <Label>Scene</Label>
          <Show
            when={!props.scenesLoading}
            fallback={<Skeleton class="h-10 w-full" />}
          >
            <ChoiceSelect
              items={props.scenes}
              value={
                (action() as Extract<ActionDraft, { type: "activate_scene" }>)
                  .sceneId
              }
              placeholder="Select scene"
              label="Scene"
              onChange={(sceneId) => props.onUpdate({ sceneId })}
            />
          </Show>
        </div>
      </Show>
      <Show when={action().type === "local_notification"}>
        <TextField
          value={
            (action() as Extract<ActionDraft, { type: "local_notification" }>)
              .message
          }
          onChange={(message) => props.onUpdate({ message })}
          class="sm:col-span-2"
        >
          <TextFieldLabel>Message</TextFieldLabel>
          <TextFieldInput
            id={`message-${props.index}`}
            maxlength={entityLimits.notification}
          />
        </TextField>
      </Show>
      <Show when={action().type === "delay"}>
        <TextField
          value={String(
            (action() as Extract<ActionDraft, { type: "delay" }>).milliseconds,
          )}
          onChange={(value) => props.onUpdate({ milliseconds: Number(value) })}
          class="sm:col-span-2"
        >
          <TextFieldLabel>Milliseconds</TextFieldLabel>
          <TextFieldInput
            id={`delay-${props.index}`}
            type="number"
            min="0"
            max="60000"
          />
        </TextField>
      </Show>
      <Show when={action().type === "legacy"}>
        <p class="text-sm text-destructive sm:col-span-2">
          Replace this legacy free-form action with a supported action.
        </p>
      </Show>
    </>
  );
}
