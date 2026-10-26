import { createEffect, createSignal, For, Show } from "solid-js";
import { createForm } from "solid-hook-form";
import { z } from "zod";
import {
  HiOutlineRectangleGroup,
  HiOutlinePencil,
  HiOutlinePlus,
  HiOutlineSparkles,
} from "solid-icons/hi";
import { toast } from "solid-sonner";

import { Alert, AlertDescription, AlertTitle } from "~/components/alert";
import { AutomationBuilder } from "~/components/automation-builder";
import { Badge } from "~/components/badge";
import { Button } from "~/components/button";
import { ConfirmDestructiveAction } from "~/components/confirm-destructive-action";
import { TooltipIconButton } from "~/components/tooltip";
import {
  Card,
  CardContent,
  CardDescription,
  CardFooter,
  CardHeader,
  CardTitle,
} from "~/components/card";
import { EmptyState } from "~/components/empty-state";
import { Flex } from "~/components/flex";
import { Grid } from "~/components/grid";
import { Separator } from "~/components/separator";
import {
  Sheet,
  SheetContent,
  SheetDescription,
  SheetFooter,
  SheetHeader,
  SheetTitle,
} from "~/components/sheet";
import { Skeleton } from "~/components/skeleton";
import {
  TextField,
  TextFieldDescription,
  TextFieldErrorMessage,
  TextFieldInput,
  TextFieldLabel,
} from "~/components/text-field";
import type { Device, Scene } from "~/lib/api";
import { bytesAtMost, entityLimits } from "~/lib/request-contracts";
import { createToastGate } from "~/lib/toast";
import {
  actionSummary,
  decodeAction,
  encodeAction,
  MAX_AUTOMATION_ACTIONS,
  validateAutomationDraft,
  type ActionDraft,
} from "~/lib/automations";
import { sceneDeleteMutation, sceneUpsertMutation } from "~/mutations/models";
import {
  automationsQuery as createAutomationsQuery,
  fallbackAutomations,
} from "~/queries/automations";
import {
  fallbackDevices,
  devicesQuery as createDevicesQuery,
} from "~/queries/devices";
import {
  fallbackScenes,
  scenesQuery as createScenesQuery,
} from "~/queries/scenes";

const sceneFormSchema = z.object({
  name: bytesAtMost(
    entityLimits.name,
    "Name must be 64 bytes or fewer.",
  ).refine((value) => value.trim().length > 0, "Enter a scene name."),
});

type SceneFormValues = z.input<typeof sceneFormSchema>;

const deviceId = (device: Device) =>
  typeof device.id === "string" ? device.id : device.id[0];

function blankAction(type: ActionDraft["type"] = "set_on_off"): ActionDraft {
  if (type === "set_on_off") return { type, deviceId: "", on: true };
  if (type === "set_level") return { type, deviceId: "", level: 50 };
  if (type === "activate_scene") return { type, sceneId: "" };
  if (type === "local_notification") return { type, message: "" };
  if (type === "delay") return { type, milliseconds: 1000 };
  return { type: "legacy", value: "" };
}

export default function Scenes() {
  const scenesQuery = createScenesQuery();
  const devicesQuery = createDevicesQuery();
  const automationsQuery = createAutomationsQuery();
  const upsertMutation = sceneUpsertMutation();
  const deleteMutation = sceneDeleteMutation();
  const [editingId, setEditingId] = createSignal<string | null>(null);
  const [isSheetOpen, setIsSheetOpen] = createSignal(false);
  const [sceneActions, setSceneActions] = createSignal<ActionDraft[]>([]);
  const [actionError, setActionError] = createSignal<string | null>(null);
  const { errors, handleSubmit, register, reset } = createForm<SceneFormValues>(
    {
      defaultValues: { name: "" },
      mode: "onSubmit",
      resolver: async (formValues: SceneFormValues) => {
        const result = sceneFormSchema.safeParse(formValues);
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
    },
  );
  const shouldShowToast = createToastGate();

  const response = () => scenesQuery.data ?? fallbackScenes;
  const devices = () => devicesQuery.data?.devices ?? fallbackDevices.devices;
  const automations = () =>
    automationsQuery.data?.automations ?? fallbackAutomations.automations;
  const sceneReferences = (sceneId: string) =>
    automations().filter((automation) =>
      automation.actions.includes(`scene|${sceneId}`),
    );
  const isInitialLoading = () => scenesQuery.isFetching && !scenesQuery.data;
  const devicesInitialLoading = () =>
    devicesQuery.isFetching && !devicesQuery.data;

  const resetForm = () => {
    setEditingId(null);
    setSceneActions([]);
    setActionError(null);
    reset({ name: "" });
  };

  const closeSheet = () => {
    setIsSheetOpen(false);
    resetForm();
  };

  const createScene = () => {
    resetForm();
    setIsSheetOpen(true);
  };

  const edit = (scene: Scene) => {
    setEditingId(scene.id);
    setSceneActions(scene.actions.map(decodeAction));
    setActionError(null);
    reset({ name: scene.name });
    setIsSheetOpen(true);
  };

  const submitScene = (formValues: SceneFormValues) => {
    const validation = validateAutomationDraft(
      formValues.name,
      { type: "manual" },
      sceneActions(),
    );
    if (validation) {
      setActionError(validation);
      return;
    }
    setActionError(null);
    upsertMutation.mutate(
      {
        id: editingId(),
        name: formValues.name.trim(),
        actions: sceneActions().map(encodeAction),
      },
      { onSuccess: (result) => result.accepted && closeSheet() },
    );
  };

  const addAction = (type: Exclude<ActionDraft["type"], "legacy">) => {
    if (sceneActions().length < MAX_AUTOMATION_ACTIONS)
      setSceneActions([...sceneActions(), blankAction(type)]);
  };

  const updateAction = (index: number, patch: Partial<ActionDraft>) => {
    setSceneActions((actions) =>
      actions.map((action, actionIndex) =>
        actionIndex === index
          ? ({ ...action, ...patch } as ActionDraft)
          : action,
      ),
    );
  };

  const setActionType = (
    index: number,
    type: Exclude<ActionDraft["type"], "legacy">,
  ) => {
    setSceneActions((actions) =>
      actions.map((action, actionIndex) =>
        actionIndex === index ? blankAction(type) : action,
      ),
    );
  };

  const moveAction = (from: number, to: number) => {
    if (
      from < 0 ||
      to < 0 ||
      from >= sceneActions().length ||
      to >= sceneActions().length
    )
      return;
    const reordered = [...sceneActions()];
    const [moved] = reordered.splice(from, 1);
    reordered.splice(to, 0, moved);
    setSceneActions(reordered);
  };

  createEffect(() => {
    const success = upsertMutation.isSuccess || deleteMutation.isSuccess;
    const error = upsertMutation.isError || deleteMutation.isError;
    const message = success
      ? (upsertMutation.data?.detail ??
        deleteMutation.data?.detail ??
        "Scene saved.")
      : error
        ? (upsertMutation.error?.message ??
          deleteMutation.error?.message ??
          "Scene change failed.")
        : "";
    const key = `${success ? "success" : "error"}:${message}`;
    if (!shouldShowToast(message ? key : "")) return;
    if (success) toast.success(message);
    if (error) toast.error(message);
  });

  return (
    <section class="w-full">
      <Card class="w-full overflow-hidden">
        <CardHeader>
          <Flex
            alignItems="start"
            justifyContent="between"
            class="flex-col gap-4 sm:flex-row"
          >
            <div class="max-w-3xl">
              <div class="mb-3 h-px w-12 bg-foreground" />
              <h1 class="text-xl font-semibold tracking-tight">Scenes</h1>
              <CardDescription>
                Capture an ordered set of local device actions and replay it
                without AI or cloud access.
              </CardDescription>
            </div>
            <Flex
              justifyContent="start"
              class="w-full flex-wrap gap-2 sm:w-auto"
            >
              <Button as="a" variant="outline" href="#/devices">
                View devices
              </Button>
              <Button type="button" onClick={createScene}>
                <HiOutlinePlus data-icon="inline-start" />
                Create scene
              </Button>
            </Flex>
          </Flex>
        </CardHeader>

        <CardContent class="p-5 sm:p-7">
          <Flex
            flexDirection="col"
            alignItems="stretch"
            justifyContent="start"
            class="min-w-0 gap-5"
          >
            <Flex
              alignItems="start"
              class="flex-col gap-3 sm:flex-row sm:items-end"
            >
              <div>
                <h2 class="text-lg font-semibold">Scene library</h2>
                <p class="text-sm text-muted-foreground">
                  Ready-to-run local compositions.
                </p>
              </div>
              <Show
                when={!isInitialLoading()}
                fallback={<Skeleton class="h-6 w-20 rounded-full" />}
              >
                <Badge variant="outline">
                  {response().scenes.length} saved
                </Badge>
              </Show>
            </Flex>
            <Separator />

            <Show
              when={!isInitialLoading()}
              fallback={
                <Grid class="gap-4" aria-busy="true" aria-live="polite">
                  <For each={[0, 1]}>
                    {() => (
                      <Card class="overflow-hidden">
                        <CardHeader class="gap-3">
                          <Flex justifyContent="start" class="gap-3">
                            <Skeleton class="size-10 shrink-0 rounded-full" />
                            <div class="min-w-0 flex-1 space-y-2">
                              <Skeleton class="h-5 w-36 max-w-full" />
                              <Skeleton class="h-4 w-20" />
                            </div>
                            <Skeleton class="h-6 w-16 rounded-full" />
                          </Flex>
                        </CardHeader>
                        <CardContent class="space-y-2">
                          <Skeleton class="h-9 w-full" />
                          <Skeleton class="h-9 w-full" />
                        </CardContent>
                        <CardFooter class="justify-end gap-2">
                          <Skeleton class="h-8 w-20" />
                          <Skeleton class="size-8" />
                        </CardFooter>
                      </Card>
                    )}
                  </For>
                </Grid>
              }
            >
              <Show
                when={response().scenes.length > 0}
                fallback={
                  <EmptyState
                    icon={<HiOutlineSparkles />}
                    title="Your scene library is empty"
                    description="Compose a validated ordered device-state draft. Saving does not execute commands until the Matter action service is available."
                    action={
                      <Button type="button" size="sm" onClick={createScene}>
                        Create scene
                      </Button>
                    }
                  />
                }
              >
                <Grid colsLg={2} class="gap-4">
                  <For each={response().scenes}>
                    {(scene, sceneIndex) => {
                      const references = () => sceneReferences(scene.id);
                      return (
                        <Card class="overflow-hidden">
                          <CardHeader>
                            <Flex alignItems="start" class="flex-wrap gap-3">
                              <Flex
                                justifyContent="start"
                                class="min-w-0 flex-1 gap-3"
                              >
                                <div
                                  class="grid size-10 shrink-0 place-items-center rounded-full bg-muted"
                                  aria-hidden="true"
                                >
                                  <HiOutlineRectangleGroup />
                                </div>
                                <div class="min-w-0">
                                  <CardTitle class="truncate">
                                    {scene.name}
                                  </CardTitle>
                                  <CardDescription>
                                    Scene{" "}
                                    {String(sceneIndex() + 1).padStart(2, "0")}
                                  </CardDescription>
                                </div>
                              </Flex>
                              <Badge variant="secondary">
                                {scene.actions.length} steps
                              </Badge>
                            </Flex>
                          </CardHeader>
                          <CardContent>
                            <Show
                              when={scene.actions.length > 0}
                              fallback={
                                <p class="text-sm text-muted-foreground">
                                  No actions configured yet.
                                </p>
                              }
                            >
                              <ol class="flex flex-col gap-2">
                                <For each={scene.actions}>
                                  {(action, index) => (
                                    <li class="flex items-center gap-3 rounded-md border px-3 py-2 text-sm">
                                      <span class="font-mono text-xs text-muted-foreground">
                                        {index() + 1}
                                      </span>
                                      <span class="truncate">
                                        {actionSummary(decodeAction(action))}
                                      </span>
                                    </li>
                                  )}
                                </For>
                              </ol>
                            </Show>
                          </CardContent>
                          <CardFooter class="justify-end gap-2">
                            <TooltipIconButton
                              type="button"
                              size="icon"
                              variant="outline"
                              aria-label={`Edit ${scene.name}`}
                              onClick={() => edit(scene)}
                            >
                              <HiOutlinePencil aria-hidden="true" />
                            </TooltipIconButton>
                            <ConfirmDestructiveAction
                              triggerLabel="Delete"
                              triggerAriaLabel={`Delete ${scene.name}`}
                              title={`Delete ${scene.name}?`}
                              description="The scene cannot be deleted while an automation references it. Remove those actions first; Core Pilot never silently rewrites automations."
                              confirmLabel="Delete scene"
                              disabled={deleteMutation.isPending}
                              blockedReason={
                                references().length > 0
                                  ? `Used by: ${references()
                                      .map((automation) => automation.name)
                                      .join(", ")}. Remove those actions first.`
                                  : undefined
                              }
                              onConfirm={() =>
                                deleteMutation.mutateAsync({ id: scene.id })
                              }
                            />
                          </CardFooter>
                        </Card>
                      );
                    }}
                  </For>
                </Grid>
              </Show>
            </Show>
          </Flex>
        </CardContent>
      </Card>

      <Sheet
        open={isSheetOpen()}
        onOpenChange={(open) => {
          if (open) setIsSheetOpen(true);
          else closeSheet();
        }}
      >
        <SheetContent class="flex w-full max-w-none flex-col gap-0 overflow-hidden p-0 sm:max-w-3xl lg:max-w-5xl">
          <SheetHeader class="shrink-0 border-b px-6 py-5 pr-12">
            <SheetTitle>
              {editingId() ? "Edit scene" : "Create scene"}
            </SheetTitle>
            <SheetDescription>
              Give the scene a concise name and one to eight ordered actions.
            </SheetDescription>
          </SheetHeader>

          <form
            class="flex min-h-0 flex-1 flex-col"
            onSubmit={handleSubmit(submitScene)}
          >
            <div class="min-h-0 flex-1 space-y-6 overflow-y-auto px-6 py-6">
              <TextField validationState={errors.name ? "invalid" : "valid"}>
                <TextFieldLabel for="scene-name">Name</TextFieldLabel>
                <TextFieldInput
                  id="scene-name"
                  maxlength={entityLimits.name}
                  placeholder="Movie night"
                  {...register("name")}
                />
                <Show
                  when={errors.name?.message}
                  fallback={
                    <TextFieldDescription>
                      Use a short name that is easy to find and say.
                    </TextFieldDescription>
                  }
                >
                  {(message) => (
                    <TextFieldErrorMessage>{message()}</TextFieldErrorMessage>
                  )}
                </Show>
              </TextField>

              <Alert>
                <HiOutlineSparkles />
                <AlertTitle>Dry preview</AlertTitle>
                <AlertDescription>
                  This readable flow is validated and saved locally. No device
                  commands are sent while editing, and scene execution remains
                  unavailable until real Matter writes are wired.
                </AlertDescription>
              </Alert>

              <AutomationBuilder
                kind="scene"
                actions={sceneActions()}
                devices={devices().map((device) => ({
                  value: deviceId(device),
                  label: device.name,
                }))}
                scenes={response()
                  .scenes.filter((scene) => scene.id !== editingId())
                  .map((scene) => ({ value: scene.id, label: scene.name }))}
                devicesLoading={devicesInitialLoading()}
                scenesLoading={isInitialLoading()}
                onAdd={addAction}
                onChangeType={setActionType}
                onUpdate={updateAction}
                onMove={moveAction}
                onRemove={(index) =>
                  setSceneActions((actions) =>
                    actions.filter((_, actionIndex) => actionIndex !== index),
                  )
                }
              />
              <Show when={actionError()}>
                {(message) => (
                  <p class="text-sm text-destructive" role="alert">
                    {message()}
                  </p>
                )}
              </Show>
            </div>

            <SheetFooter class="shrink-0 gap-2 border-t bg-background px-6 py-4">
              <Button type="button" variant="outline" onClick={closeSheet}>
                Cancel
              </Button>
              <Button type="submit" disabled={upsertMutation.isPending}>
                {editingId() ? "Update scene" : "Create scene"}
              </Button>
            </SheetFooter>
          </form>
        </SheetContent>
      </Sheet>
    </section>
  );
}
