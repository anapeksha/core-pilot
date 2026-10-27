import { createSignal, For, Show } from "solid-js";
import { toast } from "solid-sonner";
import {
  HiOutlineAdjustmentsHorizontal,
  HiOutlineBolt,
  HiOutlineCpuChip,
  HiOutlineLightBulb,
  HiOutlinePencil,
  HiOutlineSwatch,
} from "solid-icons/hi";

import { Button } from "~/components/button";
import { ConfirmDestructiveAction } from "~/components/confirm-destructive-action";
import {
  ContextMenu,
  ContextMenuContent,
  ContextMenuItem,
  ContextMenuSeparator,
  ContextMenuTrigger,
} from "~/components/context-menu";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
} from "~/components/card";
import { EmptyState } from "~/components/empty-state";
import { Flex } from "~/components/flex";

import { Skeleton } from "~/components/skeleton";
import { StatusBadge, type StatusTone } from "~/components/status";

import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "~/components/table";
import type { Device } from "~/lib/api";
import { cn } from "~/lib/utils";
import {
  matterDeviceMutation,
  matterDeviceRemoveMutation,
} from "~/mutations/matter";

import {
  fallbackDevices,
  devicesQuery as createDevicesQuery,
} from "~/queries/devices";

function DevicesSkeleton() {
  return (
    <Flex
      flexDirection="col"
      alignItems="stretch"
      class="gap-5"
      aria-busy="true"
      aria-live="polite"
    >
      <div class="scroll-fade-x overflow-x-auto">
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>Name</TableHead>
              <TableHead>Protocol</TableHead>
              <TableHead>Availability</TableHead>
              <TableHead>Capabilities</TableHead>
              <TableHead>Power</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            <For each={Array.from({ length: 3 })}>
              {() => (
                <TableRow>
                  <For each={["w-32", "w-20", "w-24", "w-40", "w-20"]}>
                    {(width) => (
                      <TableCell>
                        <Skeleton class={cn("h-4", width)} />
                      </TableCell>
                    )}
                  </For>
                </TableRow>
              )}
            </For>
          </TableBody>
        </Table>
      </div>
    </Flex>
  );
}

function deviceKey(device: { id: string | { 0: string } }): string {
  return typeof device.id === "string" ? device.id : device.id[0];
}

function availabilityTone(availability: string): StatusTone {
  const normalized = availability.toLowerCase();
  if (normalized === "online") return "ok";
  if (normalized === "offline") return "error";
  if (normalized === "stale") return "warn";
  return "unavailable";
}

function onOffState(state: unknown): boolean | undefined {
  if (!Array.isArray(state)) return undefined;
  for (const entry of state) {
    if (
      typeof entry === "object" &&
      entry !== null &&
      "OnOff" in entry &&
      typeof entry.OnOff === "boolean"
    ) {
      return entry.OnOff;
    }
  }
  return undefined;
}

function supportsMatterOnOff(device: {
  protocol: string;
  capabilities: string[];
}): boolean {
  return (
    device.protocol.toLowerCase() === "matter" &&
    device.capabilities.some(
      (capability) => capability.toLowerCase() === "on_off",
    )
  );
}

function powerControlDescription(device: Device, pending: boolean): string {
  if (pending) return `A power command for ${device.name} is pending.`;
  if (device.availability.toLowerCase() !== "online")
    return `Power control is unavailable until ${device.name} is online.`;
  if (onOffState(device.state) === undefined)
    return `The current power state is unknown. Sending a command will establish the displayed state for ${device.name}.`;
  return `Turn ${device.name} on or off.`;
}

function capabilityIcon(capability: string) {
  const normalized = capability.toLowerCase();
  if (normalized.includes("level")) return HiOutlineAdjustmentsHorizontal;
  if (normalized.includes("color")) return HiOutlineSwatch;
  if (normalized.includes("on") || normalized.includes("off"))
    return HiOutlineLightBulb;
  return HiOutlineBolt;
}

export default function Devices() {
  const devicesQuery = createDevicesQuery();
  const deviceMutation = matterDeviceMutation();
  const removeMutation = matterDeviceRemoveMutation();
  const [compact, setCompact] = createSignal(false);
  const [editingDeviceId, setEditingDeviceId] = createSignal<string | null>(
    null,
  );
  const [deviceName, setDeviceName] = createSignal("");
  const [pendingDeviceId, setPendingDeviceId] = createSignal<string | null>(
    null,
  );
  const [contextDevice, setContextDevice] = createSignal<Device | null>(null);
  const [removalDevice, setRemovalDevice] = createSignal<Device | null>(null);
  const [commandedOnOff, setCommandedOnOff] = createSignal<
    Record<string, boolean>
  >({});
  const response = () => devicesQuery.data ?? fallbackDevices;
  const displayedOnOff = (device: Device): boolean | undefined =>
    onOffState(device.state) ?? commandedOnOff()[deviceKey(device)];

  const removeDevice = async (device: Device) => {
    if (!device.node_id)
      throw new Error("Matter node identity is unavailable.");
    await removeMutation.mutateAsync({ device_id: device.node_id });
    toast.success("Matter device removal queued.");
  };

  const renameDevice = async (device: Device) => {
    if (!device.node_id || !device.endpoint_id)
      throw new Error("Matter endpoint identity is unavailable.");
    const name = deviceName().trim();
    try {
      await deviceMutation.mutateAsync({
        device_id: device.node_id,
        endpoint_id: device.endpoint_id,
        name,
      });
      setEditingDeviceId(null);
      toast.success("Device name saved.");
    } catch (error) {
      toast.error(
        error instanceof Error ? error.message : "Could not rename device.",
      );
    }
  };

  const setOnOff = async (device: Device, on: boolean) => {
    const id = deviceKey(device);
    setPendingDeviceId(id);
    try {
      if (!device.node_id || !device.endpoint_id) {
        throw new Error("Matter endpoint identity is unavailable.");
      }
      await deviceMutation.mutateAsync({
        device_id: device.node_id,
        endpoint_id: device.endpoint_id,
        on,
      });
      setCommandedOnOff((current) => ({ ...current, [id]: on }));
      toast.success(
        "Matter command queued. Power will update when the device reports state.",
      );
    } catch (error) {
      toast.error(
        error instanceof Error
          ? error.message
          : "Could not queue Matter command.",
      );
    } finally {
      setPendingDeviceId(null);
    }
  };

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
            <h1 class="text-xl font-semibold tracking-tight">Devices</h1>
            <CardDescription>
              Manage connected devices and controls
            </CardDescription>
          </div>
          <Button
            as="a"
            href="#/devices/add"
            size="sm"
            class="shrink-0 self-start"
          >
            Add device
          </Button>
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
            when={!(devicesQuery.isFetching && !devicesQuery.data)}
            fallback={<DevicesSkeleton />}
          >
            <Show
              when={response().devices.length > 0}
              fallback={
                <EmptyState
                  icon={<HiOutlineCpuChip />}
                  title="Nothing connected yet"
                  description="Matter commissioning needs the controller runtime and a real reachable device. You can inspect readiness before starting setup."
                  action={
                    <Flex justifyContent="center" class="flex-wrap gap-2">
                      <Button as="a" href="#/devices/add" size="sm">
                        Add device
                      </Button>
                      <Button
                        as="a"
                        href="#/devices/configure"
                        variant="outline"
                        size="sm"
                      >
                        Check setup
                      </Button>
                    </Flex>
                  }
                />
              }
            >
              <Flex class="flex-col items-start gap-3 sm:flex-row sm:items-center">
                <div class="min-w-0 flex-1">
                  <h2 class="font-semibold">Device inventory</h2>
                  <p class="text-sm text-muted-foreground">
                    Choose a comfortable or compact desktop density.
                  </p>
                </div>
                <div
                  class="inline-flex rounded-md border p-1"
                  aria-label="Table density"
                >
                  <Button
                    type="button"
                    size="sm"
                    variant={!compact() ? "secondary" : "ghost"}
                    aria-pressed={!compact()}
                    onClick={() => setCompact(false)}
                  >
                    Comfortable
                  </Button>
                  <Button
                    type="button"
                    size="sm"
                    variant={compact() ? "secondary" : "ghost"}
                    aria-pressed={compact()}
                    onClick={() => setCompact(true)}
                  >
                    Compact
                  </Button>
                </div>
              </Flex>
              <div class="scroll-fade-x overflow-x-auto">
                <ContextMenu>
                  <ContextMenuTrigger>
                    <Table>
                      <TableHeader>
                        <TableRow>
                          <TableHead>Name</TableHead>
                          <TableHead>Protocol</TableHead>
                          <TableHead>Availability</TableHead>
                          <TableHead>Capabilities</TableHead>
                          <TableHead>Power</TableHead>
                        </TableRow>
                      </TableHeader>
                      <TableBody>
                        <For each={response().devices}>
                          {(device) => (
                            <TableRow
                              data-device-id={deviceKey(device)}
                              class={compact() ? "h-10" : "h-14"}
                              onContextMenu={() => setContextDevice(device)}
                            >
                              <TableCell class="max-w-72 min-w-0">
                                <Show
                                  when={editingDeviceId() === deviceKey(device)}
                                  fallback={
                                    <span
                                      class="block truncate font-medium"
                                      title={device.name}
                                    >
                                      {device.name}
                                    </span>
                                  }
                                >
                                  <form
                                    class="flex min-w-56 items-center gap-2"
                                    onSubmit={(event) => {
                                      event.preventDefault();
                                      void renameDevice(device);
                                    }}
                                  >
                                    <input
                                      value={deviceName()}
                                      onInput={(event) =>
                                        setDeviceName(event.currentTarget.value)
                                      }
                                      maxlength={64}
                                      class="h-9 min-w-0 flex-1 rounded-md border bg-background px-3 text-sm focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
                                      aria-label="Device name"
                                      autofocus
                                    />
                                    <Button
                                      type="submit"
                                      size="sm"
                                      disabled={deviceMutation.isPending}
                                    >
                                      Save
                                    </Button>
                                    <Button
                                      type="button"
                                      size="sm"
                                      variant="ghost"
                                      onClick={() => setEditingDeviceId(null)}
                                    >
                                      Cancel
                                    </Button>
                                  </form>
                                </Show>
                              </TableCell>
                              <TableCell class="min-w-0 wrap-anywhere">
                                {device.protocol}
                              </TableCell>
                              <TableCell>
                                <StatusBadge
                                  tone={availabilityTone(device.availability)}
                                  label={device.availability}
                                />
                              </TableCell>
                              <TableCell class="min-w-0">
                                <div class="flex min-w-0 flex-wrap gap-2">
                                  <For each={device.capabilities ?? []}>
                                    {(capability) => {
                                      const Icon = capabilityIcon(capability);
                                      return (
                                        <span class="inline-flex items-center gap-1.5 rounded-full border px-2 py-1 text-xs">
                                          <Icon
                                            class="size-3.5"
                                            aria-hidden="true"
                                          />
                                          {capability}
                                        </span>
                                      );
                                    }}
                                  </For>
                                </div>
                              </TableCell>
                              <TableCell>
                                <Show when={supportsMatterOnOff(device)}>
                                  <span class="text-sm font-medium">
                                    {displayedOnOff(device) === undefined
                                      ? "Unknown"
                                      : displayedOnOff(device)
                                        ? "On"
                                        : "Off"}
                                  </span>
                                  <span
                                    id={`power-${deviceKey(device)}-description`}
                                    class="sr-only"
                                  >
                                    {powerControlDescription(
                                      device,
                                      deviceMutation.isPending ||
                                        pendingDeviceId() === deviceKey(device),
                                    )}
                                  </span>
                                </Show>
                              </TableCell>
                            </TableRow>
                          )}
                        </For>
                      </TableBody>
                    </Table>
                  </ContextMenuTrigger>
                  <ContextMenuContent>
                    <Show when={contextDevice()} keyed>
                      {(device) => (
                        <>
                          <ContextMenuItem
                            onSelect={() => {
                              setDeviceName(device.name);
                              setEditingDeviceId(deviceKey(device));
                            }}
                          >
                            <HiOutlinePencil aria-hidden="true" />
                            Rename
                          </ContextMenuItem>
                          <Show when={supportsMatterOnOff(device)}>
                            <ContextMenuItem
                              disabled={
                                device.availability.toLowerCase() !==
                                  "online" ||
                                deviceMutation.isPending ||
                                pendingDeviceId() === deviceKey(device)
                              }
                              onSelect={() =>
                                void setOnOff(device, !displayedOnOff(device))
                              }
                            >
                              <HiOutlineBolt aria-hidden="true" />
                              {displayedOnOff(device) ? "Turn off" : "Turn on"}
                            </ContextMenuItem>
                          </Show>
                          <Show
                            when={device.protocol.toLowerCase() === "matter"}
                          >
                            <ContextMenuSeparator />
                            <ContextMenuItem
                              class="text-error data-[highlighted]:text-error"
                              aria-label={`Remove ${device.name}`}
                              disabled={
                                removeMutation.isPending || !device.node_id
                              }
                              title={
                                removeMutation.isPending
                                  ? "Another device removal is in progress."
                                  : !device.node_id
                                    ? "Removal requires a reported Matter node identity."
                                    : undefined
                              }
                              onSelect={() => setRemovalDevice(device)}
                            >
                              Remove device…
                            </ContextMenuItem>
                          </Show>
                        </>
                      )}
                    </Show>
                  </ContextMenuContent>
                </ContextMenu>
                <Show when={removalDevice()} keyed>
                  {(device) => (
                    <ConfirmDestructiveAction
                      hideTrigger
                      open
                      onOpenChange={(open) => !open && setRemovalDevice(null)}
                      triggerLabel="Remove"
                      triggerAriaLabel={`Remove ${device.name}`}
                      title={`Remove ${device.name}?`}
                      description="This will unpair the Matter device and remove it from Core Pilot. This action cannot be undone."
                      confirmLabel="Remove device"
                      onConfirm={() => removeDevice(device)}
                      onError={(error) =>
                        toast.error(
                          error instanceof Error
                            ? error.message
                            : "Could not remove Matter device.",
                        )
                      }
                    />
                  )}
                </Show>
              </div>
            </Show>
          </Show>
        </Flex>
      </CardContent>
    </Card>
  );
}
