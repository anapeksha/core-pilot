import { createEffect, createSignal, For, Show } from "solid-js";
import { createForm } from "solid-hook-form";
import { HiOutlineHome } from "solid-icons/hi";
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
import { EmptyState } from "~/components/empty-state";
import { Flex } from "~/components/flex";
import { Grid } from "~/components/grid";
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
import { StatusBadge } from "~/components/status";
import type { Device, Room } from "~/lib/api";

import { roomAccentClass } from "~/lib/presentation";
import { bytesAtMost, entityLimits } from "~/lib/request-contracts";
import { createToastGate } from "~/lib/toast";
import { cn } from "~/lib/utils";
import { roomDeleteMutation, roomUpsertMutation } from "~/mutations/models";
import {
  devicesQuery as createDevicesQuery,
  fallbackDevices,
} from "~/queries/devices";
import { fallbackRooms, roomsQuery as createRoomsQuery } from "~/queries/rooms";

const roomSchema = z.object({
  name: bytesAtMost(
    entityLimits.name,
    "Room names must be 64 bytes or fewer.",
  ).refine((value) => value.trim().length > 0, "Enter a room name."),
});

type RoomFormValues = z.input<typeof roomSchema>;

function deviceRoomId(device: Device): string | null {
  if (!device.room) return null;
  return typeof device.room === "string" ? device.room : device.room[0];
}

export default function Rooms() {
  const roomsQuery = createRoomsQuery();
  const devicesQuery = createDevicesQuery();
  const upsertMutation = roomUpsertMutation();
  const deleteMutation = roomDeleteMutation();
  const [editingId, setEditingId] = createSignal<string | null>(null);
  const [isSheetOpen, setIsSheetOpen] = createSignal(false);
  const { errors, handleSubmit, register, reset } = createForm<RoomFormValues>({
    defaultValues: { name: "" },
    mode: "onTouched",
  });
  const nameField = register("name", {
    validate: (value) => {
      const result = roomSchema.shape.name.safeParse(value);
      return result.success || result.error.issues[0]?.message;
    },
  });
  const shouldShowToast = createToastGate();

  const response = () => roomsQuery.data ?? fallbackRooms;
  const devices = () => devicesQuery.data?.devices ?? fallbackDevices.devices;
  const devicesForRoom = (roomId: string) =>
    devices().filter((device) => deviceRoomId(device) === roomId);
  const isInitialLoading = () => roomsQuery.isFetching && !roomsQuery.data;

  const resetForm = () => {
    setIsSheetOpen(false);
    setEditingId(null);
    reset({ name: "" });
  };

  const createRoom = () => {
    setEditingId(null);
    reset({ name: "" });
    setIsSheetOpen(true);
  };

  const edit = (room: Room) => {
    setEditingId(room.id);
    reset({ name: room.name });
    setIsSheetOpen(true);
  };

  const submitRoom = handleSubmit((values) => {
    const room = roomSchema.parse(values);
    upsertMutation.mutate(
      { id: editingId(), name: room.name },
      { onSuccess: (result) => result.accepted && resetForm() },
    );
  });

  createEffect(() => {
    const success = upsertMutation.isSuccess || deleteMutation.isSuccess;
    const error = upsertMutation.isError || deleteMutation.isError;
    const successMessage =
      upsertMutation.data?.detail ??
      deleteMutation.data?.detail ??
      "Room saved.";
    const errorMessage =
      upsertMutation.error?.message ??
      deleteMutation.error?.message ??
      "Room change failed.";
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
            <h1 class="text-xl font-semibold tracking-tight">Rooms</h1>
            <CardDescription>Organize devices by location</CardDescription>
          </div>
          <Flex
            justifyContent="start"
            class="w-full flex-wrap gap-2 self-start sm:w-auto"
          >
            <Button type="button" size="sm" onClick={createRoom}>
              Create room
            </Button>
            <Button as="a" href="#/devices" variant="outline" size="sm">
              View devices
            </Button>
            <Button as="a" href="#/devices/add" size="sm">
              Add device
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
              when={response().rooms.length > 0}
              fallback={
                <EmptyState
                  icon={<HiOutlineHome />}
                  title="Create your first room"
                  description="Rooms organize reported device assignments by location. They do not control devices by themselves."
                  action={
                    <Button type="button" size="sm" onClick={createRoom}>
                      Create room
                    </Button>
                  }
                />
              }
            >
              <Grid colsMd={2} class="gap-3">
                <For each={response().rooms}>
                  {(room) => {
                    const assignedDevices = () => devicesForRoom(room.id);
                    const unavailableCount = () =>
                      assignedDevices().filter(
                        (device) =>
                          device.availability.toLowerCase() !== "online",
                      ).length;
                    return (
                      <Card class={cn("border-l-4", roomAccentClass(room.id))}>
                        <CardHeader>
                          <Flex class="flex-col items-start gap-2 sm:flex-row">
                            <div class="min-w-0 flex-1">
                              <CardTitle>{room.name}</CardTitle>
                              <CardDescription>{room.id}</CardDescription>
                            </div>
                            <StatusBadge
                              tone={
                                assignedDevices().length === 0
                                  ? "muted"
                                  : unavailableCount() > 0
                                    ? "warn"
                                    : "ok"
                              }
                              label={`${assignedDevices().length} devices`}
                            />
                          </Flex>
                        </CardHeader>
                        <CardContent>
                          <Show
                            when={assignedDevices().length > 0}
                            fallback={
                              <p class="text-sm text-muted-foreground">
                                No assigned devices are currently reported by
                                firmware.
                              </p>
                            }
                          >
                            <ul
                              class="space-y-2"
                              aria-label={`${room.name} devices`}
                            >
                              <For each={assignedDevices().slice(0, 3)}>
                                {(device) => (
                                  <li class="flex items-center justify-between gap-3 text-sm">
                                    <span class="truncate">{device.name}</span>
                                    <span class="text-xs capitalize text-muted-foreground">
                                      {device.availability}
                                    </span>
                                  </li>
                                )}
                              </For>
                            </ul>
                            <Show when={assignedDevices().length > 3}>
                              <p class="mt-2 text-xs text-muted-foreground">
                                +{assignedDevices().length - 3} more devices
                              </p>
                            </Show>
                          </Show>
                        </CardContent>
                        <CardFooter class="justify-end gap-2">
                          <Button
                            type="button"
                            variant="outline"
                            size="sm"
                            onClick={() => edit(room)}
                          >
                            Edit
                          </Button>
                          <ConfirmDestructiveAction
                            triggerLabel="Delete"
                            title={`Delete ${room.name}?`}
                            description="This removes the room record. Core Pilot will not silently change devices or other saved models."
                            confirmLabel="Delete room"
                            disabled={deleteMutation.isPending}
                            blockedReason={
                              assignedDevices().length > 0
                                ? `Assigned devices: ${assignedDevices()
                                    .map((device) => device.name)
                                    .join(
                                      ", ",
                                    )}. Reassign them before deleting this room.`
                                : undefined
                            }
                            onConfirm={() =>
                              deleteMutation.mutateAsync({ id: room.id })
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

      <Sheet
        open={isSheetOpen()}
        onOpenChange={(open) => {
          if (open) setIsSheetOpen(true);
          else resetForm();
        }}
      >
        <SheetContent
          position="right"
          class="flex w-full flex-col overflow-hidden! p-0 sm:max-w-md"
        >
          <SheetHeader class="shrink-0 border-b p-6 pr-12">
            <SheetTitle>{editingId() ? "Edit room" : "Create room"}</SheetTitle>
            <SheetDescription>
              Room names are stored in the encrypted model database.
            </SheetDescription>
          </SheetHeader>

          <form
            class="flex min-h-0 flex-1 flex-col overflow-hidden"
            onSubmit={submitRoom}
          >
            <div class="min-h-0 flex-1 overflow-y-auto p-6">
              <TextField validationState={errors.name ? "invalid" : "valid"}>
                <TextFieldLabel for="room-name">Name</TextFieldLabel>
                <TextFieldInput
                  {...nameField}
                  id="room-name"
                  maxlength={entityLimits.name}
                  placeholder="Kitchen"
                  aria-invalid={Boolean(errors.name)}
                />
                <TextFieldDescription>
                  Use a short location name, such as Kitchen.
                </TextFieldDescription>
                <Show when={errors.name?.message}>
                  {(message) => (
                    <TextFieldErrorMessage>{message()}</TextFieldErrorMessage>
                  )}
                </Show>
              </TextField>
            </div>

            <SheetFooter class="shrink-0 gap-2 border-t p-6">
              <Button type="button" variant="outline" onClick={resetForm}>
                Cancel
              </Button>
              <Button type="submit" disabled={upsertMutation.isPending}>
                {editingId() ? "Update room" : "Create room"}
              </Button>
            </SheetFooter>
          </form>
        </SheetContent>
      </Sheet>
    </Card>
  );
}
