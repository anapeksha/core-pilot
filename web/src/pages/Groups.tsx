import { createEffect, createMemo, createSignal, For, Show } from "solid-js";
import { createForm } from "solid-hook-form";
import { HiOutlinePencil, HiOutlineSquares2x2 } from "solid-icons/hi";
import { toast } from "solid-sonner";
import { z } from "zod";

import { Button } from "~/components/button";
import { ConfirmDestructiveAction } from "~/components/confirm-destructive-action";
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
import {
  Select,
  SelectContent,
  SelectDescription,
  SelectErrorMessage,
  SelectItem,
  SelectLabel,
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
import {
  TextField,
  TextFieldErrorMessage,
  TextFieldInput,
  TextFieldLabel,
} from "~/components/text-field";
import type { Device, Group } from "~/lib/api";
import { bytesAtMost, entityLimits } from "~/lib/request-contracts";
import { createToastGate } from "~/lib/toast";

import { groupDeleteMutation, groupUpsertMutation } from "~/mutations/models";
import {
  fallbackDevices,
  devicesQuery as createDevicesQuery,
} from "~/queries/devices";
import {
  fallbackGroups,
  groupsQuery as createGroupsQuery,
} from "~/queries/groups";

const groupSchema = z.object({
  name: bytesAtMost(
    entityLimits.name,
    "Name must be 64 bytes or fewer.",
  ).refine((value) => value.trim().length > 0, "Name is required."),
  members: z
    .array(bytesAtMost(entityLimits.detail, "Device ID is too long."))
    .max(entityLimits.groupMembers, "A group can contain at most 32 devices."),
});

type GroupFormValues = z.input<typeof groupSchema>;
type DeviceOption = { id: string; name: string };

function deviceId(device: Device): string {
  return typeof device.id === "string" ? device.id : device.id[0];
}

export default function Groups() {
  const groupsQuery = createGroupsQuery();
  const devicesQuery = createDevicesQuery();
  const upsertMutation = groupUpsertMutation();
  const deleteMutation = groupDeleteMutation();
  const [editingId, setEditingId] = createSignal<string | null>(null);
  const [sheetOpen, setSheetOpen] = createSignal(false);
  const { errors, handleSubmit, register, reset, setError, setValue, values } =
    createForm<GroupFormValues>({
      defaultValues: { name: "", members: [] },
    });
  const shouldShowToast = createToastGate();

  const response = () => groupsQuery.data ?? fallbackGroups;
  const deviceOptions = createMemo<DeviceOption[]>(() =>
    (devicesQuery.data?.devices ?? fallbackDevices.devices).map((device) => ({
      id: deviceId(device),
      name: device.name,
    })),
  );
  const selectedDevices = createMemo(() => {
    const selectedIds = new Set(values().members);
    return deviceOptions().filter((device) => selectedIds.has(device.id));
  });
  const isInitialLoading = () => groupsQuery.isFetching && !groupsQuery.data;

  const openCreate = () => {
    setEditingId(null);
    reset({ name: "", members: [] });
    setSheetOpen(true);
  };

  const edit = (group: Group) => {
    setEditingId(group.id);
    reset({ name: group.name, members: group.device_ids });
    setSheetOpen(true);
  };

  const closeSheet = () => {
    setSheetOpen(false);
    setEditingId(null);
    reset({ name: "", members: [] });
  };

  const handleSheetOpenChange = (open: boolean) => {
    if (!open) {
      closeSheet();
      return;
    }
    setSheetOpen(true);
  };

  const submitGroup = handleSubmit((formValues) => {
    const result = groupSchema.safeParse(formValues);
    if (!result.success) {
      for (const issue of result.error.issues) {
        const field = issue.path[0];
        if (field === "name" || field === "members") {
          setError(field, { type: "schema", message: issue.message });
        }
      }
      return;
    }

    upsertMutation.mutate(
      {
        id: editingId(),
        name: result.data.name,
        device_ids: result.data.members,
      },
      { onSuccess: (response) => response.accepted && closeSheet() },
    );
  });

  createEffect(() => {
    const success = upsertMutation.isSuccess || deleteMutation.isSuccess;
    const error = upsertMutation.isError || deleteMutation.isError;
    const successMessage =
      upsertMutation.data?.detail ??
      deleteMutation.data?.detail ??
      "Group saved.";
    const errorMessage =
      upsertMutation.error?.message ??
      deleteMutation.error?.message ??
      "Group change failed.";
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
          class="flex-col gap-4 sm:flex-row sm:items-center"
        >
          <div class="max-w-3xl">
            <div class="mb-3 h-px w-12 bg-foreground" />
            <h1 class="text-xl font-semibold tracking-tight">Groups</h1>
            <CardDescription>Control multiple devices together</CardDescription>
          </div>
          <Flex
            justifyContent="start"
            class="w-full flex-wrap gap-2 self-start sm:w-auto"
          >
            <Button as="a" href="#/devices" variant="outline" size="sm">
              View devices
            </Button>
            <Button as="a" href="#/devices/add" variant="outline" size="sm">
              Add device
            </Button>
            <Button type="button" size="sm" onClick={openCreate}>
              Create group
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
          <Sheet open={sheetOpen()} onOpenChange={handleSheetOpenChange}>
            <SheetContent
              position="right"
              class="flex w-full max-w-none flex-col gap-0 overflow-hidden! p-0 sm:max-w-md"
            >
              <SheetHeader class="border-b px-6 py-5 pr-12">
                <SheetTitle>
                  {editingId() ? "Edit group" : "Create group"}
                </SheetTitle>
                <SheetDescription>
                  {editingId()
                    ? "Update this group and its assigned devices."
                    : "Create a group to control devices together."}
                </SheetDescription>
              </SheetHeader>
              <form class="flex min-h-0 flex-1 flex-col" onSubmit={submitGroup}>
                <div class="min-h-0 flex-1 space-y-4 overflow-y-auto px-6 py-5">
                  <TextField
                    validationState={errors.name ? "invalid" : "valid"}
                  >
                    <TextFieldLabel for="group-name">Name</TextFieldLabel>
                    <TextFieldInput
                      {...register("name", {
                        validate: (value) => {
                          const result =
                            groupSchema.shape.name.safeParse(value);
                          return (
                            result.success || result.error.issues[0]?.message
                          );
                        },
                      })}
                      id="group-name"
                      value={values().name}
                      maxlength={entityLimits.name}
                      placeholder="Downstairs lights"
                      required
                    />
                    <Show when={errors.name?.message}>
                      {(message) => (
                        <TextFieldErrorMessage>
                          {message()}
                        </TextFieldErrorMessage>
                      )}
                    </Show>
                  </TextField>
                  <Select<DeviceOption>
                    multiple
                    closeOnSelection={false}
                    options={deviceOptions()}
                    optionValue="id"
                    optionTextValue="name"
                    value={selectedDevices()}
                    placeholder="Select devices"
                    validationState={errors.members ? "invalid" : "valid"}
                    onChange={(devices) =>
                      setValue(
                        "members",
                        devices.map((device) => device.id),
                      )
                    }
                    itemComponent={(itemProps) => (
                      <SelectItem item={itemProps.item}>
                        {itemProps.item.rawValue.name}
                      </SelectItem>
                    )}
                  >
                    <SelectLabel>Devices</SelectLabel>
                    <SelectTrigger id="group-members">
                      <SelectValue<DeviceOption>>
                        {(state) => {
                          const selected = state.selectedOptions();
                          return selected.length > 0
                            ? selected.map((device) => device.name).join(", ")
                            : "Select devices";
                        }}
                      </SelectValue>
                    </SelectTrigger>
                    <SelectContent />
                    <SelectDescription>
                      Optional; select up to 32 devices.
                    </SelectDescription>
                    <Show when={errors.members?.message}>
                      {(message) => (
                        <SelectErrorMessage>{message()}</SelectErrorMessage>
                      )}
                    </Show>
                  </Select>
                </div>
                <SheetFooter class="shrink-0 gap-2 border-t px-6 py-4">
                  <Button type="button" variant="outline" onClick={closeSheet}>
                    Cancel
                  </Button>
                  <Button type="submit" disabled={upsertMutation.isPending}>
                    {editingId() ? "Update group" : "Create group"}
                  </Button>
                </SheetFooter>
              </form>
            </SheetContent>
          </Sheet>

          <Show
            when={!isInitialLoading()}
            fallback={
              <Grid colsMd={2} class="gap-3" aria-busy="true">
                <For each={[0, 1]}>
                  {() => (
                    <Card>
                      <CardHeader class="gap-2">
                        <Skeleton class="h-5 w-32" />
                        <Skeleton class="h-4 w-44" />
                      </CardHeader>
                      <CardContent>
                        <Skeleton class="h-4 w-3/4" />
                      </CardContent>
                      <CardFooter class="justify-end gap-2">
                        <Skeleton class="h-8 w-16" />
                        <Skeleton class="h-8 w-16" />
                      </CardFooter>
                    </Card>
                  )}
                </For>
              </Grid>
            }
          >
            <Show
              when={response().groups.length > 0}
              fallback={
                <Flex
                  flexDirection="col"
                  justifyContent="center"
                  class="min-h-48 gap-3 rounded-lg border p-6 text-center"
                >
                  <div
                    class="grid size-10 place-items-center rounded-full bg-muted"
                    aria-hidden="true"
                  >
                    <HiOutlineSquares2x2 />
                  </div>
                  <div>
                    <h2 class="font-semibold">Nothing configured yet</h2>
                    <p class="text-sm text-muted-foreground">
                      Create groups to control multiple devices together.
                    </p>
                  </div>
                </Flex>
              }
            >
              <Grid colsMd={2} class="gap-3">
                <For each={response().groups}>
                  {(group) => (
                    <Card>
                      <CardHeader>
                        <CardTitle>{group.name}</CardTitle>
                        <CardDescription>{group.id}</CardDescription>
                      </CardHeader>
                      <CardContent>
                        <p class="text-sm text-muted-foreground">
                          {group.device_ids.length
                            ? group.device_ids.join(", ")
                            : "No devices assigned"}
                        </p>
                      </CardContent>
                      <CardFooter class="justify-end gap-2">
                        <Button
                          type="button"
                          variant="outline"
                          size="icon"
                          aria-label={`Edit ${group.name}`}
                          title={`Edit ${group.name}`}
                          onClick={() => edit(group)}
                        >
                          <HiOutlinePencil aria-hidden="true" />
                        </Button>
                        <ConfirmDestructiveAction
                          triggerLabel="Delete"
                          triggerAriaLabel={`Delete ${group.name}`}
                          title={`Delete ${group.name}?`}
                          description="This removes the group definition, not the devices in it. Other saved models are not silently changed."
                          confirmLabel="Delete group"
                          disabled={deleteMutation.isPending}
                          onConfirm={() =>
                            deleteMutation.mutateAsync({ id: group.id })
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
