import { createEffect, createMemo, createSignal, For, Show } from "solid-js";
import {
  HiOutlineArrowPath,
  HiOutlineBolt,
  HiOutlineExclamationTriangle,
  HiOutlineHome,
} from "solid-icons/hi";
import { toast } from "solid-sonner";

import { Button } from "~/components/button";
import { CardQueryContent } from "~/components/card-query-content";
import { PageHeader } from "~/components/page-header";

import {
  Card,
  CardDescription,
  CardHeader,
  CardTitle,
} from "~/components/card";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "~/components/dialog";
import { EmptyState } from "~/components/empty-state";
import { Flex } from "~/components/flex";
import { Grid } from "~/components/grid";
import { HomeFavorites } from "~/components/home-favorites";
import { Skeleton } from "~/components/skeleton";
import { StatusBadge, StatusIcon, type StatusTone } from "~/components/status";
import {
  fallbackHealth,
  fallbackNetwork,
  fallbackStorage,
  fallbackSystem,
  type Device,
} from "~/lib/api";
import { roomAccentClass } from "~/lib/presentation";
import { createToastGate } from "~/lib/toast";
import { cn } from "~/lib/utils";
import { automationRunMutation } from "~/mutations/models";
import { systemRebootMutation } from "~/mutations/system";
import {
  automationsQuery as createAutomationsQuery,
  fallbackAutomations,
} from "~/queries/automations";
import {
  devicesQuery as createDevicesQuery,
  fallbackDevices,
} from "~/queries/devices";
import { healthQuery as createHealthQuery } from "~/queries/health";
import { networkQuery as createNetworkQuery } from "~/queries/network";
import { fallbackRooms, roomsQuery as createRoomsQuery } from "~/queries/rooms";
import { storageQuery as createStorageQuery } from "~/queries/storage";
import { systemQuery as createSystemQuery } from "~/queries/system";

function deviceRoomId(device: Device): string | null {
  if (!device.room) return null;
  return typeof device.room === "string" ? device.room : device.room[0];
}

function availabilityTone(availability: string): StatusTone {
  const normalized = availability.toLowerCase();
  if (normalized === "online") return "ok";
  if (normalized === "offline") return "error";
  if (normalized === "stale") return "warn";
  return "unavailable";
}

function HomeStatusSkeleton() {
  return (
    <div class="space-y-5">
      <div class="grid grid-cols-1 gap-3 sm:grid-cols-2 lg:grid-cols-4">
        <For each={["w-1/2", "w-2/3", "w-2/5", "w-3/5"]}>
          {(width) => (
            <div
              class="min-w-0 rounded-lg border bg-muted/15 p-4"
              data-home-status-skeleton
            >
              <div class="flex items-center justify-between gap-3">
                <Skeleton height={12} class={width} />
                <Skeleton
                  height={28}
                  width={28}
                  class="shrink-0 rounded-full"
                />
              </div>
              <Skeleton height={20} class="mt-3 w-4/5" />
            </div>
          )}
        </For>
      </div>
      <div class="space-y-3 border-t pt-4">
        <div class="flex items-center gap-3">
          <Skeleton class="h-4 w-12" />
          <Skeleton class="h-5 w-14 rounded-full" />
        </div>
        <Skeleton class="h-4 w-3/5" />
      </div>
    </div>
  );
}

function QuickActionsSkeleton() {
  return (
    <Grid cols={1} class="gap-2 @min-[26rem]/card:grid-cols-2">
      <For each={[0, 1, 2, 3]}>
        {() => <Skeleton height={40} class="w-full" />}
      </For>
    </Grid>
  );
}

function RoomsSkeleton() {
  return (
    <Grid cols={1} class="gap-3 @min-[26rem]/card:grid-cols-2">
      <For each={[0, 1]}>
        {() => (
          <div class="@container/room rounded-lg border border-l-4 p-3">
            <Flex class="flex-col items-start gap-3 @min-[18rem]/room:flex-row @min-[18rem]/room:items-center">
              <div class="w-full min-w-0 flex-1 space-y-2">
                <Skeleton height={16} class="w-1/2" />
                <Skeleton height={12} class="w-2/3" />
              </div>
              <Skeleton height={24} class="w-20 shrink-0 rounded-full" />
            </Flex>
          </div>
        )}
      </For>
    </Grid>
  );
}

function DeviceAvailabilitySkeleton() {
  return (
    <ul class="space-y-2">
      <For each={["w-2/5", "w-1/2"]}>
        {(width) => (
          <li class="flex items-center justify-between gap-3 rounded-md border p-3">
            <Skeleton class={`h-4 ${width}`} />
            <Skeleton class="h-6 w-20 shrink-0 rounded-full" />
          </li>
        )}
      </For>
    </ul>
  );
}

export default function Overview() {
  const healthQuery = createHealthQuery();
  const systemQuery = createSystemQuery();
  const networkQuery = createNetworkQuery();
  const storageQuery = createStorageQuery();
  const devicesQuery = createDevicesQuery();
  const roomsQuery = createRoomsQuery();
  const automationsQuery = createAutomationsQuery();
  const rebootMutation = systemRebootMutation();
  const runMutation = automationRunMutation();
  const [rebootDialogOpen, setRebootDialogOpen] = createSignal(false);
  const shouldShowToast = createToastGate();

  const health = () => healthQuery.data ?? fallbackHealth;
  const system = () => systemQuery.data ?? fallbackSystem;
  const network = () => networkQuery.data ?? fallbackNetwork;
  const storage = () => storageQuery.data ?? fallbackStorage;
  const devices = () => devicesQuery.data?.devices ?? fallbackDevices.devices;
  const rooms = () => roomsQuery.data?.rooms ?? fallbackRooms.rooms;
  const automations = () =>
    automationsQuery.data?.automations ?? fallbackAutomations.automations;

  const unavailableDevices = createMemo(() =>
    devices().filter(
      (device) => device.availability.toLowerCase() !== "online",
    ),
  );
  const manualAutomations = createMemo(() =>
    automations().filter(
      (automation) => automation.enabled && automation.trigger === "manual",
    ),
  );
  const alerts = createMemo(() => {
    const items: {
      title: string;
      detail: string;
      tone: StatusTone;
      href: string;
      action: string;
    }[] = [];
    if (!network().station_ipv4)
      items.push({
        title: "Home Wi-Fi is not connected",
        detail: "Review the reported network state and recovery options.",
        href: "#/system",
        action: "Review network setup",
        tone: "warn",
      });
    if (!storage().encrypted || !storage().mutations_allowed)
      items.push({
        title: "Encrypted storage needs attention",
        detail: storage().detail,
        href: "#/storage",
        action: "Review storage",
        tone: "error",
      });
    if (unavailableDevices().length > 0)
      items.push({
        title: `${unavailableDevices().length} device states are stale or offline`,
        detail:
          "Operational Matter subscriptions are required for fresh state.",
        href: "#/devices",
        action: "Review devices",
        tone: "warn",
      });
    if (
      ["prepared", "staging", "applying", "activated", "failed"].includes(
        system().ota_phase,
      )
    )
      items.push({
        title: `Firmware update: ${system().ota_phase}`,
        detail: system().ota,
        href: "#/system",
        action: "Review firmware update",
        tone: system().ota_phase === "failed" ? "error" : "warn",
      });
    return items;
  });

  const homeStatusItems = () => [
    {
      label: "Hub",
      value: `${health().hostname} · ${system().firmware_version}`,
      tone: health().storage_ok ? ("ok" as const) : ("warn" as const),
    },
    {
      label: "Network",
      value: network().station_ipv4
        ? `${network().station_ssid ?? "Joined Wi‑Fi"} · ${network().station_ipv4}`
        : `${network().setup_ssid} · ${network().ipv4}`,
      tone: network().station_ipv4 ? ("ok" as const) : ("warn" as const),
    },
    {
      label: "Storage",
      value:
        storage().encrypted && storage().mutations_allowed
          ? "Encrypted · writable"
          : storage().state,
      tone:
        storage().encrypted && storage().mutations_allowed
          ? ("ok" as const)
          : ("error" as const),
    },
    {
      label: "Updates",
      value:
        system().ota_phase === "idle"
          ? "No update in progress"
          : system().ota_phase,
      tone:
        system().ota_phase === "failed"
          ? ("error" as const)
          : system().ota_phase === "idle"
            ? ("muted" as const)
            : ("pending" as const),
    },
  ];

  createEffect(() => {
    const success = rebootMutation.isSuccess;
    const error = rebootMutation.isError;
    const successMessage =
      rebootMutation.data?.detail ?? "Core Pilot is rebooting.";
    const errorMessage =
      rebootMutation.error?.message ?? "Reboot request failed.";
    const key = success
      ? `success:${successMessage}`
      : error
        ? `error:${errorMessage}`
        : "";
    if (!shouldShowToast(key)) return;
    if (success) toast.success(successMessage);
    if (error) toast.error(errorMessage);
  });

  const requestReboot = () => {
    if (!rebootMutation.isPending) setRebootDialogOpen(true);
  };
  const closeRebootDialog = () => {
    if (!rebootMutation.isPending) setRebootDialogOpen(false);
  };
  const confirmReboot = () => {
    if (rebootMutation.isPending) return;
    setRebootDialogOpen(false);
    rebootMutation.mutate();
  };

  const runAutomation = async (id: string) => {
    try {
      const result = await runMutation.mutateAsync({ id });
      toast.success(result.detail ?? "Automation completed.");
    } catch (error) {
      const detail =
        error instanceof Error
          ? error.message
          : "Automation could not be completed.";
      toast.error(detail);
    }
  };

  return (
    <>
      <div class="w-full min-w-0 space-y-4">
        <PageHeader
          title="Home"
          description="Devices, rooms, automations, alerts, and hub health at a glance."
          actions={
            <>
              <Button as="a" href="#/automations" variant="outline" size="sm">
                Automations
              </Button>
              <Button as="a" href="#/devices/add" size="sm">
                Add device
              </Button>
            </>
          }
        />
        <Grid cols={1} colsLg={2} class="gap-4">
          <Card class="min-w-0 lg:col-span-2">
            <CardHeader>
              <Flex class="flex-col items-start gap-3 sm:flex-row">
                <div class="min-w-0 flex-1">
                  <CardTitle>Home status</CardTitle>
                  <CardDescription>
                    Essential hub health and conditions that need attention
                  </CardDescription>
                </div>
                <Flex class="w-auto shrink-0 flex-wrap gap-2">
                  <Button
                    type="button"
                    size="sm"
                    variant="outline"
                    disabled={rebootMutation.isPending}
                    onClick={requestReboot}
                  >
                    <HiOutlineArrowPath
                      class={cn(rebootMutation.isPending && "animate-spin")}
                      data-icon="inline-start"
                    />
                    {rebootMutation.isPending ? "Rebooting…" : "Reboot"}
                  </Button>
                  <Button as="a" href="#/system" variant="outline" size="sm">
                    Network setup
                  </Button>
                </Flex>
              </Flex>
            </CardHeader>
            <CardQueryContent
              label="home status"
              queries={[
                healthQuery,
                systemQuery,
                networkQuery,
                storageQuery,
                devicesQuery,
              ]}
              fallback={<HomeStatusSkeleton />}
            >
              <div class="space-y-5">
                <dl class="grid grid-cols-1 gap-3 sm:grid-cols-2 lg:grid-cols-4">
                  <For each={homeStatusItems()}>
                    {(item) => (
                      <div class="min-w-0 rounded-lg border bg-muted/15 p-4 transition-colors hover:bg-muted/25">
                        <dt class="flex items-center justify-between gap-3">
                          <span class="text-xs font-medium uppercase tracking-wide text-muted-foreground">
                            {item.label}
                          </span>
                          <StatusIcon tone={item.tone} />
                        </dt>
                        <dd class="mt-3 min-w-0 wrap-break-word text-sm font-semibold leading-5">
                          {item.value}
                        </dd>
                      </div>
                    )}
                  </For>
                </dl>

                <section
                  class="border-t pt-4"
                  aria-labelledby="home-alerts-title"
                >
                  <h2 id="home-alerts-title" class="sr-only">
                    Alerts
                  </h2>
                  <Show
                    when={alerts().length > 0}
                    fallback={
                      <p class="text-sm text-muted-foreground">
                        No active network, storage, device-state, or OTA
                        warnings.
                      </p>
                    }
                  >
                    <ul class="space-y-2" aria-label="Active alerts">
                      <For each={alerts()}>
                        {(alert) => (
                          <li class="flex items-start gap-3 text-sm">
                            <HiOutlineExclamationTriangle
                              class={cn(
                                "mt-0.5 size-5 shrink-0",
                                alert.tone === "error"
                                  ? "text-destructive"
                                  : "text-warning",
                              )}
                              aria-hidden="true"
                            />
                            <div class="min-w-0 flex-1">
                              <p class="text-sm font-medium">{alert.title}</p>
                              <p class="wrap-break-word text-xs text-muted-foreground">
                                {alert.detail}
                              </p>
                            </div>
                            <a
                              href={alert.href}
                              class="shrink-0 text-xs underline underline-offset-4"
                            >
                              {alert.action}
                            </a>
                          </li>
                        )}
                      </For>
                    </ul>
                  </Show>
                </section>
              </div>
            </CardQueryContent>
          </Card>

          <HomeFavorites />

          <Card class="@container/card min-w-0">
            <CardHeader>
              <CardTitle>Quick actions</CardTitle>
              <CardDescription>
                Real local actions available from the current firmware
              </CardDescription>
            </CardHeader>
            <CardQueryContent
              label="quick actions"
              queries={[automationsQuery]}
              fallback={<QuickActionsSkeleton />}
            >
              <div class="space-y-3">
                <Show
                  when={manualAutomations().length > 0}
                  fallback={
                    <p class="text-sm text-muted-foreground">
                      Create an enabled manual automation to add a working quick
                      action.
                    </p>
                  }
                >
                  <Grid cols={1} class="gap-2 @min-[26rem]/card:grid-cols-2">
                    <For each={manualAutomations().slice(0, 4)}>
                      {(automation) => (
                        <Button
                          type="button"
                          variant="outline"
                          disabled={runMutation.isPending}
                          onClick={() => void runAutomation(automation.id)}
                          class="h-auto w-full min-w-0 whitespace-normal py-3"
                        >
                          <HiOutlineBolt data-icon="inline-start" />
                          <span class="min-w-0 wrap-break-word">
                            {automation.name}
                          </span>
                        </Button>
                      )}
                    </For>
                  </Grid>
                </Show>
              </div>
            </CardQueryContent>
          </Card>

          <Card class="@container/card min-w-0">
            <CardHeader>
              <CardTitle>Rooms</CardTitle>
              <CardDescription>
                Reported device assignments by location
              </CardDescription>
            </CardHeader>
            <CardQueryContent
              label="rooms"
              queries={[roomsQuery, devicesQuery]}
              fallback={<RoomsSkeleton />}
            >
              <Show
                when={rooms().length > 0}
                fallback={
                  <EmptyState
                    icon={<HiOutlineHome />}
                    title="No rooms yet"
                    description="Create rooms to organize reported device assignments."
                    action={
                      <Button as="a" href="#/rooms" size="sm">
                        Create room
                      </Button>
                    }
                  />
                }
              >
                <Grid cols={1} class="gap-3 @min-[26rem]/card:grid-cols-2">
                  <For each={rooms().slice(0, 6)}>
                    {(room) => {
                      const assigned = () =>
                        devices().filter(
                          (device) => deviceRoomId(device) === room.id,
                        );
                      const unavailable = () =>
                        assigned().filter(
                          (device) =>
                            device.availability.toLowerCase() !== "online",
                        ).length;
                      return (
                        <div
                          class={cn(
                            "@container/room min-w-0 rounded-lg border border-l-4 p-3",
                            roomAccentClass(room.id),
                          )}
                        >
                          <Flex class="min-w-0 flex-col items-start gap-3 @min-[18rem]/room:flex-row @min-[18rem]/room:items-center">
                            <div class="w-full min-w-0 flex-1">
                              <p
                                class="wrap-break-word font-medium"
                                title={room.name}
                              >
                                {room.name}
                              </p>
                              <p class="text-xs text-muted-foreground">
                                {assigned().length} reported devices
                              </p>
                            </div>
                            <StatusBadge
                              class="shrink-0"
                              tone={
                                assigned().length === 0
                                  ? "muted"
                                  : unavailable() > 0
                                    ? "warn"
                                    : "ok"
                              }
                              label={
                                unavailable() > 0
                                  ? `${unavailable()} unavailable`
                                  : assigned().length > 0
                                    ? "Available"
                                    : "Empty"
                              }
                            />
                          </Flex>
                        </div>
                      );
                    }}
                  </For>
                </Grid>
              </Show>
            </CardQueryContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle>Offline or stale devices</CardTitle>
              <CardDescription>
                Availability reported by the current device inventory
              </CardDescription>
            </CardHeader>
            <CardQueryContent
              label="device availability"
              queries={[devicesQuery]}
              fallback={<DeviceAvailabilitySkeleton />}
            >
              <Show
                when={unavailableDevices().length > 0}
                fallback={
                  <p class="text-sm text-muted-foreground">
                    No offline or stale devices are currently reported.
                  </p>
                }
              >
                <ul class="space-y-2">
                  <For each={unavailableDevices().slice(0, 8)}>
                    {(device) => (
                      <li class="flex items-center justify-between gap-3 rounded-md border p-3">
                        <span
                          class="min-w-0 flex-1 wrap-break-word text-sm font-medium"
                          title={device.name}
                        >
                          {device.name}
                        </span>
                        <StatusBadge
                          class="shrink-0"
                          tone={availabilityTone(device.availability)}
                          label={device.availability}
                        />
                      </li>
                    )}
                  </For>
                </ul>
              </Show>
            </CardQueryContent>
          </Card>
        </Grid>
        <footer class="border-t pt-3 text-sm">
          <a href="#/activity" class="underline underline-offset-4">
            View activity history
          </a>
        </footer>
      </div>

      <Dialog open={rebootDialogOpen()} onOpenChange={setRebootDialogOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Reboot Core Pilot?</DialogTitle>
            <DialogDescription>
              The dashboard will disconnect while the device restarts.
            </DialogDescription>
          </DialogHeader>
          <p class="text-sm text-muted-foreground">
            Reboot only if no pairing, OTA, or storage operation is in progress.
          </p>
          <DialogFooter>
            <Button variant="outline" onClick={closeRebootDialog}>
              Cancel
            </Button>
            <Button disabled={rebootMutation.isPending} onClick={confirmReboot}>
              <Show when={rebootMutation.isPending}>
                <HiOutlineArrowPath
                  class="animate-spin"
                  data-icon="inline-start"
                />
              </Show>
              Reboot
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </>
  );
}
