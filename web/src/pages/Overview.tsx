import {
  createEffect,
  createMemo,
  createSignal,
  For,
  type JSX,
  onMount,
  Show,
} from "solid-js";
import {
  HiOutlineArrowPath,
  HiOutlineBolt,
  HiOutlineExclamationTriangle,
  HiOutlineHome,
  HiOutlineStar,
} from "solid-icons/hi";
import { toast } from "solid-sonner";

import { Button } from "~/components/button";

import {
  Card,
  CardContent,
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
import {
  fallbackScenes,
  scenesQuery as createScenesQuery,
} from "~/queries/scenes";
import { storageQuery as createStorageQuery } from "~/queries/storage";
import { systemQuery as createSystemQuery } from "~/queries/system";

const FAVORITES_KEY = "core-pilot-home-favorites";
const MAX_FAVORITES = 8;

function deviceId(device: Device): string {
  return typeof device.id === "string" ? device.id : device.id[0];
}

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

function queryErrorMessage(error: unknown): string {
  return error instanceof Error
    ? error.message
    : "The firmware did not return this data.";
}

function CardQueryContent(props: {
  loading: boolean;
  error: unknown;
  label: string;
  fallback: JSX.Element;
  children: JSX.Element;
}) {
  return (
    <CardContent>
      <Show
        when={!props.error}
        fallback={
          <div role="alert" class="rounded-md border border-destructive/40 p-3">
            <p class="text-sm font-medium">Could not load {props.label}</p>
            <p class="mt-1 text-xs text-muted-foreground">
              {queryErrorMessage(props.error)}
            </p>
          </div>
        }
      >
        <Show
          when={!props.loading}
          fallback={
            <div
              role="status"
              aria-busy="true"
              data-home-card-skeleton={props.label}
            >
              <span class="sr-only">Loading {props.label}</span>
              {props.fallback}
            </div>
          }
        >
          {props.children}
        </Show>
      </Show>
    </CardContent>
  );
}

function HomeStatusSkeleton() {
  return (
    <div class="space-y-5">
      <dl class="grid grid-cols-1 gap-3 sm:grid-cols-2 lg:grid-cols-4">
        <For each={["w-1/2", "w-2/3", "w-2/5", "w-3/5"]}>
          {(width) => (
            <div
              class="min-w-0 rounded-lg border bg-muted/15 p-4"
              data-home-status-skeleton
            >
              <div class="flex items-center justify-between gap-3">
                <Skeleton class={`h-3 ${width}`} />
                <Skeleton class="size-7 rounded-full" />
              </div>
              <Skeleton class="mt-3 h-5 w-4/5" />
            </div>
          )}
        </For>
      </dl>
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
    <div class="space-y-3">
      <div class="grid grid-cols-1 gap-2 sm:grid-cols-2">
        <Skeleton class="h-9 rounded-md" />
        <Skeleton class="h-9 rounded-md" />
      </div>
      <div class="grid grid-cols-1 gap-2 border-t pt-3 sm:grid-cols-2">
        <Skeleton class="h-9 rounded-md" />
        <Skeleton class="h-9 rounded-md" />
      </div>
    </div>
  );
}

function FavoritesSkeleton() {
  return (
    <div class="space-y-4">
      <div class="grid grid-cols-1 gap-2 sm:grid-cols-2">
        <For each={[0, 1]}>
          {() => (
            <div class="flex items-center gap-3 rounded-md border p-3">
              <Skeleton class="size-5 shrink-0 rounded-full" />
              <div class="flex-1 space-y-2">
                <Skeleton class="h-4 w-3/5" />
                <Skeleton class="h-3 w-2/5" />
              </div>
              <Skeleton class="h-8 w-16 rounded-md" />
            </div>
          )}
        </For>
      </div>
      <div class="space-y-2 border-t pt-3">
        <Skeleton class="h-3 w-24" />
        <div class="flex gap-2">
          <Skeleton class="h-8 w-24 rounded-md" />
          <Skeleton class="h-8 w-28 rounded-md" />
        </div>
      </div>
    </div>
  );
}

function RoomsSkeleton() {
  return (
    <Grid cols={1} colsSm={2} class="gap-3">
      <For each={[0, 1]}>
        {() => (
          <div class="flex items-center gap-3 rounded-lg border border-l-4 p-3">
            <div class="flex-1 space-y-2">
              <Skeleton class="h-4 w-1/2" />
              <Skeleton class="h-3 w-2/3" />
            </div>
            <Skeleton class="size-7 rounded-full" />
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
            <Skeleton class="h-6 w-20 rounded-full" />
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
  const scenesQuery = createScenesQuery();
  const automationsQuery = createAutomationsQuery();
  const rebootMutation = systemRebootMutation();
  const runMutation = automationRunMutation();
  const [rebootDialogOpen, setRebootDialogOpen] = createSignal(false);
  const [favoriteKeys, setFavoriteKeys] = createSignal<string[]>([]);
  const shouldShowToast = createToastGate();

  const health = () => healthQuery.data ?? fallbackHealth;
  const system = () => systemQuery.data ?? fallbackSystem;
  const network = () => networkQuery.data ?? fallbackNetwork;
  const storage = () => storageQuery.data ?? fallbackStorage;
  const devices = () => devicesQuery.data?.devices ?? fallbackDevices.devices;
  const rooms = () => roomsQuery.data?.rooms ?? fallbackRooms.rooms;
  const scenes = () => scenesQuery.data?.scenes ?? fallbackScenes.scenes;
  const automations = () =>
    automationsQuery.data?.automations ?? fallbackAutomations.automations;

  onMount(() => {
    try {
      const stored = JSON.parse(localStorage.getItem(FAVORITES_KEY) ?? "[]");
      if (Array.isArray(stored))
        setFavoriteKeys(
          stored
            .filter((value): value is string => typeof value === "string")
            .slice(0, MAX_FAVORITES),
        );
    } catch {
      localStorage.removeItem(FAVORITES_KEY);
    }
  });

  const saveFavorites = (favorites: string[]) => {
    const bounded = favorites.slice(0, MAX_FAVORITES);
    setFavoriteKeys(bounded);
    localStorage.setItem(FAVORITES_KEY, JSON.stringify(bounded));
  };

  const toggleFavorite = (key: string) => {
    const current = favoriteKeys();
    saveFavorites(
      current.includes(key)
        ? current.filter((favorite) => favorite !== key)
        : [...current, key],
    );
  };

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
    const items: { title: string; detail: string; tone: StatusTone }[] = [];
    if (!network().station_ipv4)
      items.push({
        title: "Home Wi-Fi is not connected",
        detail: "The setup network remains available for local recovery.",
        tone: "warn",
      });
    if (!storage().encrypted || !storage().mutations_allowed)
      items.push({
        title: "Encrypted storage needs attention",
        detail: storage().detail,
        tone: "error",
      });
    if (unavailableDevices().length > 0)
      items.push({
        title: `${unavailableDevices().length} device states are stale or offline`,
        detail:
          "Operational Matter subscriptions are required for fresh state.",
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
      toast.success(result.detail ?? "Automation queued.");
    } catch (error) {
      const detail =
        error instanceof Error
          ? error.message
          : "Automation could not be queued.";
      toast.error(detail);
    }
  };

  return (
    <>
      <Card class="w-full overflow-hidden">
        <CardHeader>
          <Flex
            alignItems="start"
            justifyContent="between"
            class="flex-col gap-4 sm:flex-row"
          >
            <div class="max-w-3xl">
              <div class="mb-3 h-px w-12 bg-foreground" />
              <h1 class="text-xl font-semibold tracking-tight">Home</h1>
              <CardDescription>
                Devices, rooms, automations, alerts, and hub health at a glance.
              </CardDescription>
            </div>
            <Flex
              class="w-full flex-wrap gap-2 sm:w-auto"
              justifyContent="start"
            >
              <Button as="a" href="#/automations" variant="outline" size="sm">
                Automations
              </Button>
              <Button as="a" href="#/devices/add" size="sm">
                Add device
              </Button>
            </Flex>
          </Flex>
        </CardHeader>
        <CardContent>
          <Grid cols={1} colsLg={2} class="gap-4">
            <Card class="lg:col-span-2">
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
                loading={
                  healthQuery.isPlaceholderData ||
                  systemQuery.isPlaceholderData ||
                  networkQuery.isPlaceholderData ||
                  storageQuery.isPlaceholderData ||
                  devicesQuery.isPlaceholderData
                }
                error={
                  healthQuery.error ??
                  systemQuery.error ??
                  networkQuery.error ??
                  storageQuery.error ??
                  devicesQuery.error
                }
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
                    <Flex class="mb-3 gap-3">
                      <h2 id="home-alerts-title" class="text-sm font-semibold">
                        Alerts
                      </h2>
                      <StatusBadge
                        tone={alerts().length > 0 ? "warn" : "ok"}
                        label={
                          alerts().length > 0
                            ? `${alerts().length} active`
                            : "Clear"
                        }
                      />
                    </Flex>
                    <Show
                      when={alerts().length > 0}
                      fallback={
                        <p class="text-sm text-muted-foreground">
                          No active network, storage, device-state, or OTA
                          warnings.
                        </p>
                      }
                    >
                      <ul
                        class="grid grid-cols-1 gap-3 lg:grid-cols-2"
                        aria-label="Active alerts"
                      >
                        <For each={alerts()}>
                          {(alert) => (
                            <li class="flex items-start gap-3 rounded-md border p-3">
                              <HiOutlineExclamationTriangle
                                class="mt-0.5 size-5 shrink-0 text-warning"
                                aria-hidden="true"
                              />
                              <div class="min-w-0 flex-1">
                                <p class="text-sm font-medium">{alert.title}</p>
                                <p class="text-xs text-muted-foreground">
                                  {alert.detail}
                                </p>
                              </div>
                              <StatusBadge tone={alert.tone} />
                            </li>
                          )}
                        </For>
                      </ul>
                    </Show>
                  </section>
                </div>
              </CardQueryContent>
            </Card>

            <Card>
              <CardHeader>
                <CardTitle>Quick actions</CardTitle>
                <CardDescription>
                  Real local actions available from the current firmware
                </CardDescription>
              </CardHeader>
              <CardQueryContent
                label="quick actions"
                loading={automationsQuery.isPlaceholderData}
                error={automationsQuery.error}
                fallback={<QuickActionsSkeleton />}
              >
                <div class="space-y-3">
                  <Show
                    when={manualAutomations().length > 0}
                    fallback={
                      <p class="text-sm text-muted-foreground">
                        Create an enabled manual automation to add a working
                        quick action.
                      </p>
                    }
                  >
                    <div class="grid grid-cols-1 gap-2 sm:grid-cols-2">
                      <For each={manualAutomations().slice(0, 4)}>
                        {(automation) => (
                          <Button
                            type="button"
                            variant="outline"
                            disabled={runMutation.isPending}
                            onClick={() => void runAutomation(automation.id)}
                          >
                            <HiOutlineBolt data-icon="inline-start" />
                            {automation.name}
                          </Button>
                        )}
                      </For>
                    </div>
                  </Show>
                </div>
              </CardQueryContent>
            </Card>

            <Card>
              <CardHeader>
                <CardTitle>Favorites</CardTitle>
                <CardDescription>
                  Stored only in this browser, with a maximum of eight
                </CardDescription>
              </CardHeader>
              <CardQueryContent
                label="favorites"
                loading={
                  devicesQuery.isPlaceholderData ||
                  scenesQuery.isPlaceholderData
                }
                error={devicesQuery.error ?? scenesQuery.error}
                fallback={<FavoritesSkeleton />}
              >
                <div class="space-y-3">
                  <p class="text-sm text-muted-foreground">
                    Select or deselect stars below. Favorites stay on this
                    browser only.
                  </p>
                  <div class="flex min-w-0 flex-wrap gap-2">
                    <For each={devices()}>
                      {(device) => (
                        <Button
                          type="button"
                          size="sm"
                          variant={
                            favoriteKeys().includes(
                              `device:${deviceId(device)}`,
                            )
                              ? "secondary"
                              : "outline"
                          }
                          aria-pressed={favoriteKeys().includes(
                            `device:${deviceId(device)}`,
                          )}
                          onClick={() =>
                            toggleFavorite(`device:${deviceId(device)}`)
                          }
                          disabled={
                            !favoriteKeys().includes(
                              `device:${deviceId(device)}`,
                            ) && favoriteKeys().length >= MAX_FAVORITES
                          }
                          title={
                            !favoriteKeys().includes(
                              `device:${deviceId(device)}`,
                            ) && favoriteKeys().length >= MAX_FAVORITES
                              ? "Remove a favorite before adding another."
                              : undefined
                          }
                          class="max-w-full min-w-0"
                        >
                          <HiOutlineStar data-icon="inline-start" />
                          <span class="truncate">{device.name}</span>
                        </Button>
                      )}
                    </For>
                    <For each={scenes()}>
                      {(scene) => (
                        <Button
                          type="button"
                          size="sm"
                          variant={
                            favoriteKeys().includes(`scene:${scene.id}`)
                              ? "secondary"
                              : "outline"
                          }
                          aria-pressed={favoriteKeys().includes(
                            `scene:${scene.id}`,
                          )}
                          onClick={() => toggleFavorite(`scene:${scene.id}`)}
                          disabled={
                            !favoriteKeys().includes(`scene:${scene.id}`) &&
                            favoriteKeys().length >= MAX_FAVORITES
                          }
                          title={
                            !favoriteKeys().includes(`scene:${scene.id}`) &&
                            favoriteKeys().length >= MAX_FAVORITES
                              ? "Remove a favorite before adding another."
                              : undefined
                          }
                          class="max-w-full min-w-0"
                        >
                          <HiOutlineStar data-icon="inline-start" />
                          <span class="truncate">{scene.name}</span>
                        </Button>
                      )}
                    </For>
                  </div>
                </div>
              </CardQueryContent>
            </Card>

            <Card>
              <CardHeader>
                <CardTitle>Rooms</CardTitle>
                <CardDescription>
                  Reported device assignments by location
                </CardDescription>
              </CardHeader>
              <CardQueryContent
                label="rooms"
                loading={
                  roomsQuery.isPlaceholderData || devicesQuery.isPlaceholderData
                }
                error={roomsQuery.error ?? devicesQuery.error}
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
                  <Grid cols={1} colsSm={2} class="gap-3">
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
                              "rounded-lg border border-l-4 p-3",
                              roomAccentClass(room.id),
                            )}
                          >
                            <Flex class="min-w-0 gap-3">
                              <div class="min-w-0 flex-1">
                                <p
                                  class="truncate font-medium"
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
                loading={devicesQuery.isPlaceholderData}
                error={devicesQuery.error}
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
                          <span class="truncate text-sm font-medium">
                            {device.name}
                          </span>
                          <StatusBadge
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

            <Card>
              <CardHeader>
                <CardTitle>Activity history</CardTitle>
                <CardDescription>
                  View retained firmware events, including automation and
                  configuration outcomes.
                </CardDescription>
              </CardHeader>
              <CardContent>
                <Button as="a" href="#/activity" variant="outline">
                  <HiOutlineBolt data-icon="inline-start" />
                  View activity
                </Button>
              </CardContent>
            </Card>
          </Grid>
        </CardContent>
      </Card>

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
