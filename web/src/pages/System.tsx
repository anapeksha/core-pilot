import { useLocation, useNavigate } from "@solidjs/router";
import {
  createEffect,
  createMemo,
  createSignal,
  For,
  onCleanup,
  onMount,
  Show,
} from "solid-js";
import { createForm } from "solid-hook-form";
import { toast } from "solid-sonner";

import { bytesAtMost } from "~/lib/request-contracts";
import { createToastGate } from "~/lib/toast";
import { z } from "zod";
import {
  HiOutlineArrowUpTray,
  HiOutlineCheck,
  HiOutlineDocumentText,
  HiOutlineLockClosed,
  HiOutlineTrash,
  HiOutlineWifi,
} from "solid-icons/hi";

import { Alert } from "~/components/alert";
import { Badge } from "~/components/badge";
import { Button } from "~/components/button";
import { ConfirmDestructiveAction } from "~/components/confirm-destructive-action";
import { TooltipIconButton } from "~/components/tooltip";
import {
  Combobox,
  ComboboxContent,
  ComboboxControl,
  ComboboxInput,
  ComboboxItem,
  ComboboxItemIndicator,
  ComboboxItemLabel,
  ComboboxTrigger,
} from "~/components/combobox";
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
import { Progress } from "~/components/progress";
import {
  Sheet,
  SheetContent,
  SheetDescription,
  SheetFooter,
  SheetHeader,
  SheetTitle,
} from "~/components/sheet";

import { Skeleton } from "~/components/skeleton";
import { Spinner } from "~/components/spinner";
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
  TextFieldErrorMessage,
  TextFieldInput,
  TextFieldLabel,
} from "~/components/text-field";
import {
  fallbackNetwork,
  fallbackSystem,
  fallbackTime,
  fallbackWifiScan,
  MAX_OTA_IMAGE_BYTES,
  NTP_SERVER_OPTIONS,
  runWifiScanSession,
  type WifiNetwork,
} from "~/lib/api";
import {
  firmwareVersionFromFilename,
  formatOtaSize,
  OTA_CHUNK_BYTES,
  otaChunkCanRetry,
  otaChunkWasCommitted,
  validateFirmwareCandidate,
} from "~/lib/ota";
import { sha256FileHex } from "~/lib/sha256";
import type { StatusItem } from "~/lib/status";
import { wifiForgetMutation, wifiProvisionMutation } from "~/mutations/network";
import { otaChunkMutation, otaPrepareMutation } from "~/mutations/ota";
import { timeConfigMutation } from "~/mutations/system";
import { networkQuery } from "~/queries/network";
import { savedWifiQuery } from "~/queries/savedWifi";
import { systemQuery } from "~/queries/system";
import { timeQuery } from "~/queries/time";

const wifiSchema = z.object({
  ssid: bytesAtMost(32, "SSID must be 32 bytes or fewer.")
    .refine(
      (value) => value.trim().length > 0,
      "Select a network or enter its SSID.",
    )
    .transform((value) => value.trim()),
  password: bytesAtMost(63, "Password must be 63 bytes or fewer.").refine(
    (value) =>
      value.length === 0 || new TextEncoder().encode(value).length >= 8,
    "Password must be empty or at least 8 bytes.",
  ),
  hostname: bytesAtMost(69, "Device name is too long.")
    .transform((value) => value.trim())
    .transform((value) =>
      value.toLowerCase().endsWith(".local") ? value.slice(0, -6) : value,
    )
    .refine(
      (value) =>
        value.length > 0 &&
        value.length <= 63 &&
        /^[A-Za-z0-9-]+$/.test(value) &&
        !value.startsWith("-") &&
        !value.endsWith("-"),
      "Device name must be one DNS label (for example, corepilot).",
    ),
});

type WifiFormValues = z.input<typeof wifiSchema>;

const timeSchema = z.object({
  ntpServerId: z
    .string()
    .refine(
      (value) => NTP_SERVER_OPTIONS.some((option) => option.id === value),
      "Choose an approved NTP server.",
    ),
  timezoneId: z.string().min(1, "Choose a timezone."),
});

type TimeFormValues = z.input<typeof timeSchema>;

const otaSchema = z.object({
  file: z.custom<File>((value) => value instanceof File, {
    message: "Select a firmware image first.",
  }),
  restartAfterUpdate: z.boolean(),
});

type OtaFormValues = { file: File | null; restartAfterUpdate: boolean };

function statusToneClass(tone: StatusItem["tone"]): string {
  return tone === "muted" ? "text-foreground/55" : "text-foreground";
}

function StatusGrid(props: { items: StatusItem[] }) {
  return (
    <Grid cols={1} colsMd={2} class="gap-3">
      <For each={props.items}>
        {(item) => (
          <Card>
            <CardHeader class="gap-1">
              <CardDescription>{item.label}</CardDescription>
              <CardTitle class={statusToneClass(item.tone)}>
                {item.value}
              </CardTitle>
            </CardHeader>
          </Card>
        )}
      </For>
    </Grid>
  );
}

type TimeOption = { value: string; label: string };

function supportedTimeZones(): string[] {
  const supportedValuesOf = Intl.supportedValuesOf?.bind(Intl);
  const zones = supportedValuesOf ? supportedValuesOf("timeZone") : [];
  return Array.from(new Set(["UTC", ...zones]));
}

function timezoneOffsetMinutes(zone: string, at = new Date()): number {
  const parts = new Intl.DateTimeFormat("en-US", {
    timeZone: zone,
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
    hour12: false,
  }).formatToParts(at);
  const value = (type: string) =>
    Number(parts.find((part) => part.type === type)?.value ?? 0);
  const asUtc = Date.UTC(
    value("year"),
    value("month") - 1,
    value("day"),
    value("hour") % 24,
    value("minute"),
    value("second"),
  );
  return Math.round((asUtc - at.getTime()) / 60000);
}

function timezoneOffsetLabel(offsetMinutes: number): string {
  const sign = offsetMinutes < 0 ? "-" : "+";
  const absolute = Math.abs(offsetMinutes);
  return `UTC${sign}${String(Math.floor(absolute / 60)).padStart(2, "0")}:${String(absolute % 60).padStart(2, "0")}`;
}

function timezonePlaceLabel(zone: string): string {
  const city = zone.split("/").slice(1).join(" / ").replace(/_/g, " ");
  const region = zone.split("/")[0]?.replace(/_/g, " ") || "UTC";
  return city ? `${region} — ${city}` : region;
}

function timezoneLabel(zone: string): string {
  const offset = timezoneOffsetLabel(timezoneOffsetMinutes(zone));
  return zone === "UTC"
    ? `UTC · ${offset}`
    : `${timezonePlaceLabel(zone)} · ${offset}`;
}

const SUPPORTED_TIMEZONE_OPTIONS: TimeOption[] = supportedTimeZones()
  .map((zone) => ({ value: zone, label: timezoneLabel(zone) }))
  .sort((left, right) => left.label.localeCompare(right.label));

function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  for (let index = 0; index < bytes.length; index += 1)
    binary += String.fromCharCode(bytes[index]);
  return globalThis.btoa(binary);
}

function sleep(ms: number) {
  return new Promise<void>((resolve) => globalThis.setTimeout(resolve, ms));
}

export default function System() {
  const location = useLocation();
  const navigate = useNavigate();
  const systemResult = systemQuery();
  const networkResult = networkQuery();
  const savedWifiResult = savedWifiQuery();
  const timeResult = timeQuery();
  const wifiMutation = wifiProvisionMutation();
  const wifiForget = wifiForgetMutation();
  const timeMutation = timeConfigMutation();
  const otaMutation = otaPrepareMutation();
  const otaChunk = otaChunkMutation();
  const system = () => systemResult.data ?? fallbackSystem;
  const network = () => networkResult.data ?? fallbackNetwork;
  const time = () => timeResult.data ?? fallbackTime;
  const savedWifiNetworks = () => savedWifiResult.data?.networks ?? [];

  const [wifiSheetOpen, setWifiSheetOpen] = createSignal(false);
  const [timeSheetOpen, setTimeSheetOpen] = createSignal(false);
  const [selectedWifiNetworkId, setSelectedWifiNetworkId] = createSignal("");
  const wifiForm = createForm<WifiFormValues>({
    defaultValues: { ssid: "", password: "", hostname: "corepilot" },
  });
  const [wifiScanState, setWifiScanState] = createSignal(fallbackWifiScan);
  const [wifiScanLoading, setWifiScanLoading] = createSignal(false);
  const [wifiSetupStep, setWifiSetupStep] = createSignal(0);
  let wifiScanAbort: AbortController | null = null;
  let digestAbort: AbortController | null = null;

  const timeForm = createForm<TimeFormValues>({
    defaultValues: {
      ntpServerId: "cloudflare",
      timezoneId: Intl.DateTimeFormat().resolvedOptions().timeZone || "UTC",
    },
  });
  const [ntpComboboxOpen, setNtpComboboxOpen] = createSignal(false);
  const [timezoneComboboxOpen, setTimezoneComboboxOpen] = createSignal(false);
  const [loadedTimeConfigKey, setLoadedTimeConfigKey] = createSignal("");
  const [nowMs, setNowMs] = createSignal(Date.now());
  const [timeBaseUnixSeconds, setTimeBaseUnixSeconds] = createSignal<
    number | null
  >(null);
  const [timeBaseLoadedMs, setTimeBaseLoadedMs] = createSignal(Date.now());

  const [otaVersion, setOtaVersion] = createSignal("");
  const [otaSize, setOtaSize] = createSignal(0);
  const [otaSizeLabel, setOtaSizeLabel] = createSignal("Select a .bin file");
  const [otaSha256, setOtaSha256] = createSignal("");
  const otaForm = createForm<OtaFormValues>({
    defaultValues: { file: null, restartAfterUpdate: true },
  });
  const [preparingDigest, setPreparingDigest] = createSignal(false);
  const [digestProgressBytes, setDigestProgressBytes] = createSignal(0);
  const [otaProgressBytes, setOtaProgressBytes] = createSignal(0);
  const [otaProgressTotal, setOtaProgressTotal] = createSignal(0);
  const [otaStage, setOtaStage] = createSignal("Idle");
  const [otaUploadVisible, setOtaUploadVisible] = createSignal(false);
  const [otaClientError, setOtaClientError] = createSignal("");
  const shouldShowWifiToast = createToastGate();
  const shouldShowServiceToast = createToastGate();

  const ntpOptions: TimeOption[] = NTP_SERVER_OPTIONS.map((option) => ({
    value: option.id,
    label: option.label,
  }));
  const timezoneOptions = createMemo(() => {
    const firmwareTimezone = time();
    const options = SUPPORTED_TIMEZONE_OPTIONS.map((option) => ({
      ...option,
      label:
        option.value === firmwareTimezone.timezone_id
          ? firmwareTimezone.timezone_label
          : option.label,
    }));
    if (
      firmwareTimezone.timezone_id &&
      !options.some((option) => option.value === firmwareTimezone.timezone_id)
    ) {
      options.unshift({
        label: firmwareTimezone.timezone_label,
        value: firmwareTimezone.timezone_id,
      });
    }
    return options;
  });

  createEffect(() => {
    const status = time();
    const key = `${status.ntp_server_id}|${status.timezone_id}|${status.timezone_offset_minutes}`;
    if (loadedTimeConfigKey() !== key) {
      setLoadedTimeConfigKey(key);
      timeForm.setValue("ntpServerId", status.ntp_server_id);
      timeForm.setValue("timezoneId", status.timezone_id);
    }
  });

  createEffect(() => {
    const currentUnixSeconds = time().current_unix_seconds;
    if (
      currentUnixSeconds != null &&
      currentUnixSeconds !== timeBaseUnixSeconds()
    ) {
      setTimeBaseUnixSeconds(currentUnixSeconds);
      setTimeBaseLoadedMs(Date.now());
    }
  });

  onMount(() => {
    const interval = globalThis.setInterval(() => setNowMs(Date.now()), 1000);
    onCleanup(() => {
      wifiScanAbort?.abort();
      digestAbort?.abort();
      globalThis.clearInterval(interval);
    });
  });

  const activeNetworkName = () =>
    network().station_ssid ?? network().setup_ssid;
  const activeNetworkAddress = () => network().station_ipv4 ?? network().ipv4;
  const activeNetworkMode = () =>
    network().station_ipv4 ? "Joined Wi‑Fi" : "Setup network";
  const activeTimeSource = () =>
    NTP_SERVER_OPTIONS.find((option) => option.id === time().ntp_server_id)
      ?.label ?? time().ntp_server_host;
  const firmwareOtaInProgress = () =>
    ["prepared", "staging", "applying", "rebooting"].includes(
      system().ota_phase,
    );
  const firmwareOtaNeedsReboot = () =>
    system().ota_phase === "activated" && !system().ota_restart_after_update;
  const firmwareOtaProgressPercent = () =>
    system().ota_total_bytes
      ? Math.min(
          100,
          Math.round(
            (system().ota_progress_bytes / system().ota_total_bytes) * 100,
          ),
        )
      : 0;

  const currentDisplayUnixSeconds = () => {
    const base = timeBaseUnixSeconds();
    return base == null
      ? null
      : base + Math.floor((nowMs() - timeBaseLoadedMs()) / 1000);
  };
  const timeFormatter = createMemo(
    () =>
      new Intl.DateTimeFormat("en-US", {
        timeZone: time().timezone_id,
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
        hour12: false,
      }),
  );
  const dateFormatter = createMemo(
    () =>
      new Intl.DateTimeFormat("en-CA", {
        timeZone: time().timezone_id,
        year: "numeric",
        month: "2-digit",
        day: "2-digit",
      }),
  );
  const displayedCurrentTime = () => {
    const seconds = currentDisplayUnixSeconds();
    return seconds == null
      ? null
      : timeFormatter().format(new Date(seconds * 1000));
  };
  const displayedCurrentDate = () => {
    const seconds = currentDisplayUnixSeconds();
    return seconds == null
      ? null
      : dateFormatter().format(new Date(seconds * 1000));
  };

  const openWifiSheet = () => {
    wifiForm.reset({
      ssid: network().station_ssid ?? "",
      password: "",
      hostname: system().hostname,
    });
    setSelectedWifiNetworkId("");
    setWifiSetupStep(0);
    setWifiSheetOpen(true);
    void refreshWifiScan();
  };

  const closeWifiSheet = () => {
    wifiScanAbort?.abort();
    wifiScanAbort = null;
    setWifiScanLoading(false);
    setWifiSheetOpen(false);
    setSelectedWifiNetworkId("");
    setWifiSetupStep(0);
    wifiForm.reset({ ssid: "", password: "", hostname: "corepilot" });
  };
  const submitWifi = wifiForm.handleSubmit((formValues) => {
    const result = wifiSchema.safeParse(formValues);
    if (!result.success) {
      for (const issue of result.error.issues) {
        const field = issue.path[0];
        if (field === "ssid" || field === "password" || field === "hostname")
          wifiForm.setError(field, { type: "schema", message: issue.message });
      }
      return;
    }
    wifiMutation.mutate(
      {
        credentials: { ssid: result.data.ssid, password: result.data.password },
        hostname: result.data.hostname.toLowerCase().endsWith(".local")
          ? result.data.hostname.slice(0, -6)
          : result.data.hostname || undefined,
      },
      { onSuccess: (response) => response.accepted && closeWifiSheet() },
    );
  });

  const selectedTimezoneOffsetMinutes = (timezoneId: string) =>
    timezoneId === time().timezone_id
      ? time().timezone_offset_minutes
      : timezoneOffsetMinutes(timezoneId);

  const resetTimeForm = () =>
    timeForm.reset({
      ntpServerId: time().ntp_server_id,
      timezoneId: time().timezone_id,
    });
  const closeTimeSheet = () => {
    setTimeSheetOpen(false);
    resetTimeForm();
  };
  const saveTimeConfig = timeForm.handleSubmit((formValues) => {
    const result = timeSchema.safeParse(formValues);
    if (!result.success) {
      for (const issue of result.error.issues) {
        const field = issue.path[0];
        if (field === "ntpServerId" || field === "timezoneId")
          timeForm.setError(field, { type: "schema", message: issue.message });
      }
      return;
    }
    timeMutation.mutate(
      {
        ntp_server_id: result.data.ntpServerId,
        timezone_id: result.data.timezoneId,
        timezone_offset_minutes: selectedTimezoneOffsetMinutes(
          result.data.timezoneId,
        ),
      },
      { onSuccess: (response) => response.accepted && closeTimeSheet() },
    );
  });

  const isConnectedWifiNetwork = (wifiNetwork: WifiNetwork) =>
    wifiNetwork.connected ||
    Boolean(
      network().station_bssid &&
      wifiNetwork.bssid.toLowerCase() ===
        network().station_bssid?.toLowerCase(),
    );
  const selectedWifiNetwork = () =>
    wifiScanState().networks.find(
      (item) =>
        item.id === selectedWifiNetworkId() ||
        (!selectedWifiNetworkId() && item.ssid === wifiForm.values().ssid),
    );
  const isSavedWifiNetwork = (ssid: string) =>
    savedWifiNetworks().some((item) => item.ssid === ssid);
  const wifiSignalLabel = (signalDbm: number) =>
    signalDbm >= -55 ? "Strong" : signalDbm >= -70 ? "Good" : "Weak";
  const wifiSignalTone = (signalDbm: number) =>
    signalDbm >= -55
      ? "text-success"
      : signalDbm >= -70
        ? "text-warning"
        : "text-error";

  const selectWifiNetwork = (wifiNetwork: WifiNetwork) => {
    if (isConnectedWifiNetwork(wifiNetwork)) return;
    setSelectedWifiNetworkId(wifiNetwork.id);
    wifiForm.setValue("ssid", wifiNetwork.ssid);
    wifiForm.setValue("password", "");
  };

  const forgetSavedWifi = async (ssid: string) => {
    try {
      const response = await wifiForget.mutateAsync({ ssid });
      toast.success(response.detail);
      if (wifiForm.values().ssid === ssid) {
        setSelectedWifiNetworkId("");
        wifiForm.setValue("ssid", "");
        wifiForm.setValue("password", "");
      }
    } catch (error) {
      toast.error(
        error instanceof Error
          ? error.message
          : "Saved network was not forgotten.",
      );
      throw error;
    }
  };

  const refreshWifiScan = async () => {
    wifiScanAbort?.abort();
    const controller = new AbortController();
    wifiScanAbort = controller;
    setWifiScanLoading(true);
    setWifiScanState((state) => ({
      ...state,
      scanning: true,
      scan_state: "scanning",
      detail: "Scanning nearby Wi‑Fi networks…",
    }));
    try {
      const result = await runWifiScanSession(controller.signal, (page) => {
        setWifiScanState({
          networks: page.networks,
          scanning: page.scanning,
          scan_state: page.scan_state,
          detail: page.detail,
        });
        if (page.networks.length > 0) setWifiScanLoading(false);
      });
      setWifiScanState(result);
    } catch (error) {
      if (!controller.signal.aborted) {
        setWifiScanState((state) => ({
          ...state,
          scanning: false,
          scan_state: "error",
          detail: error instanceof Error ? error.message : "Wi‑Fi scan failed.",
        }));
      }
    } finally {
      if (wifiScanAbort === controller) {
        wifiScanAbort = null;
        setWifiScanLoading(false);
      }
    }
  };

  let handledIntent = "";
  createEffect(() => {
    const intent = new URLSearchParams(location.search).get("intent") ?? "";
    if (intent !== "wifi-scan" || handledIntent === intent) return;
    handledIntent = intent;
    openWifiSheet();
    navigate("/system", { replace: true });
  });

  const digestLabel = () =>
    otaSha256() ||
    (preparingDigest()
      ? "Calculating SHA‑256…"
      : otaForm.values().file
        ? "Digest unavailable"
        : "Select a firmware image first");
  const otaProgressPercent = () =>
    otaProgressTotal()
      ? Math.min(
          100,
          Math.round(
            ((preparingDigest() ? digestProgressBytes() : otaProgressBytes()) /
              otaProgressTotal()) *
              100,
          ),
        )
      : 0;
  const otaSelectionError = (file = otaForm.values().file) =>
    validateFirmwareCandidate(file);
  const otaUploadDisabledReason = () =>
    !otaForm.values().file
      ? "Select a firmware image first."
      : otaClientError() ||
        (!otaSha256() ? "Wait for SHA‑256 calculation to finish." : "");

  const clearFirmware = () => {
    digestAbort?.abort();
    digestAbort = null;
    otaForm.setValue("file", null);
    setOtaSize(0);
    setOtaSizeLabel("Select a .bin file");
    setOtaSha256("");
    setOtaVersion("");
    setDigestProgressBytes(0);
    setOtaProgressBytes(0);
    setOtaProgressTotal(0);
    setOtaStage("Idle");
    setOtaUploadVisible(false);
    setOtaClientError("");
  };

  const copyOtaSha256 = async () => {
    if (!otaSha256()) return;
    try {
      await navigator.clipboard.writeText(otaSha256());
    } catch {
      const textarea = document.createElement("textarea");
      textarea.value = otaSha256();
      textarea.setAttribute("readonly", "");
      textarea.style.position = "fixed";
      textarea.style.opacity = "0";
      document.body.append(textarea);
      textarea.select();
      document.execCommand("copy");
      textarea.remove();
    }
    toast.success("SHA‑256 copied.");
  };

  const selectFirmware = async (
    event: Event & { currentTarget: HTMLInputElement },
  ) => {
    const file = event.currentTarget.files?.[0];
    if (!file) return;
    digestAbort?.abort();
    const controller = new AbortController();
    digestAbort = controller;
    otaForm.setValue("file", file);
    setOtaSize(file.size);
    setOtaSizeLabel(formatOtaSize(file.size));
    setOtaVersion(firmwareVersionFromFilename(file.name));
    setOtaSha256("");
    setOtaClientError(otaSelectionError(file));
    setDigestProgressBytes(0);
    setOtaProgressBytes(0);
    setOtaProgressTotal(file.size);
    setOtaStage("Calculating digest");
    setOtaUploadVisible(false);
    setPreparingDigest(true);
    try {
      const digest = await sha256FileHex(
        file,
        (hashedBytes) => {
          if (digestAbort !== controller) return;
          setDigestProgressBytes(hashedBytes);
          setOtaStage(
            `Hashing ${Math.min(100, Math.round((hashedBytes / file.size) * 100))}%`,
          );
        },
        controller.signal,
      );
      if (digestAbort !== controller) return;
      if (!/^[0-9a-f]{64}$/i.test(digest))
        throw new Error("SHA‑256 digest could not be calculated.");
      setOtaSha256(digest.toLowerCase());
      setOtaStage(otaClientError() || "Ready to upload");
    } catch (error) {
      if (controller.signal.aborted || digestAbort !== controller) return;
      setOtaSha256("");
      setOtaClientError(
        error instanceof Error ? error.message : "Digest failed",
      );
      setOtaStage(otaClientError());
      toast.error(otaClientError());
    } finally {
      if (digestAbort === controller) {
        digestAbort = null;
        setPreparingDigest(false);
      }
    }
  };

  const refetchOtaProgress = async () =>
    (await systemResult.refetch()).data ?? system();
  const uploadOtaChunkWithProgressRecovery = async (
    offset: number,
    chunk: Uint8Array,
    finalChunk: boolean,
  ) => {
    const request = {
      offset,
      data_base64: bytesToBase64(chunk),
      final_chunk: finalChunk,
    };
    const expectedEndOffset = offset + chunk.length;
    let lastError: unknown;
    for (let attempt = 0; attempt < 2; attempt += 1) {
      try {
        return await otaChunk.mutateAsync(request);
      } catch (error) {
        lastError = error;
        const status = await refetchOtaProgress();
        if (
          otaChunkWasCommitted(
            status,
            otaProgressTotal(),
            expectedEndOffset,
            finalChunk,
          )
        ) {
          otaChunk.reset();
          return {
            accepted: true,
            status: status.ota_phase,
            detail:
              "Chunk was written, but the browser lost the response; continuing from confirmed device progress.",
          };
        }
        if (
          attempt === 0 &&
          otaChunkCanRetry(status, otaProgressTotal(), offset)
        ) {
          otaChunk.reset();
          await sleep(500);
          continue;
        }
        throw error;
      }
    }
    throw lastError;
  };

  const submitOta = otaForm.handleSubmit(async (formValues) => {
    const result = otaSchema.safeParse(formValues);
    if (!result.success) {
      for (const issue of result.error.issues) {
        const field = issue.path[0];
        if (field === "file" || field === "restartAfterUpdate")
          otaForm.setError(field, { type: "schema", message: issue.message });
      }
      return;
    }
    const file = result.data.file;
    if (!otaSha256() || otaMutation.isPending || otaChunk.isPending) return;
    const clientError = otaSelectionError(file);
    if (clientError) {
      setOtaClientError(clientError);
      setOtaStage(clientError);
      setOtaUploadVisible(false);
      toast.error(clientError);
      return;
    }
    setOtaClientError("");
    setOtaProgressBytes(0);
    setOtaProgressTotal(file.size);
    setOtaStage("Validating metadata");
    setOtaUploadVisible(true);
    try {
      const prepared = await otaMutation.mutateAsync({
        version: otaVersion().trim(),
        size_bytes: otaSize(),
        sha256_hex: otaSha256().trim(),
        restart_after_update: result.data.restartAfterUpdate,
      });
      if (!prepared.accepted) {
        const message = prepared.detail || "Rejected";
        setOtaClientError(message);
        setOtaStage(message);
        setOtaUploadVisible(false);
        toast.error(message);
        return;
      }
      await sleep(500);
      const chunkCount = Math.ceil(file.size / OTA_CHUNK_BYTES);
      let offset = 0;
      while (offset < file.size) {
        const chunkNumber = Math.floor(offset / OTA_CHUNK_BYTES) + 1;
        setOtaStage(`Uploading firmware chunk ${chunkNumber}/${chunkCount}`);
        const chunk = new Uint8Array(
          await file.slice(offset, offset + OTA_CHUNK_BYTES).arrayBuffer(),
        );
        const stagedBytes = offset + chunk.length;
        const response = await uploadOtaChunkWithProgressRecovery(
          offset,
          chunk,
          stagedBytes >= file.size,
        );
        if (!response.accepted) {
          const message = response.detail || "Chunk rejected";
          setOtaClientError(message);
          setOtaStage(message);
          setOtaUploadVisible(false);
          toast.error(message);
          return;
        }
        offset = stagedBytes;
        setOtaProgressBytes(offset);
        if (offset < file.size) await sleep(250);
      }
      setOtaStage(
        result.data.restartAfterUpdate
          ? "Applied; rebooting"
          : "Applied; reboot pending",
      );
      setOtaUploadVisible(false);
      void systemResult.refetch();
    } catch (error) {
      const message =
        error instanceof Error ? error.message : "Firmware upload failed";
      setOtaClientError(message);
      setOtaStage(message);
      setOtaUploadVisible(false);
      toast.error(message);
      void systemResult.refetch();
    }
  });

  createEffect(() => {
    const success = wifiMutation.isSuccess;
    const error = wifiMutation.isError;
    const successMessage = wifiMutation.data?.detail ?? "Wi‑Fi settings saved.";
    const errorMessage =
      wifiMutation.error?.message ?? "Wi‑Fi settings could not be saved.";
    const key = success
      ? `success:${successMessage}`
      : error
        ? `error:${errorMessage}`
        : "";
    if (!shouldShowWifiToast(key)) return;
    if (success) toast.success(successMessage);
    if (error) toast.error(errorMessage);
  });

  createEffect(() => {
    const success =
      otaMutation.isSuccess || otaChunk.isSuccess || timeMutation.isSuccess;
    const error =
      otaMutation.isError || otaChunk.isError || timeMutation.isError;
    const successMessage =
      otaChunk.data?.detail ??
      otaMutation.data?.detail ??
      timeMutation.data?.detail ??
      "Update details prepared.";
    const errorMessage =
      otaMutation.error?.message ??
      otaChunk.error?.message ??
      timeMutation.error?.message ??
      "Update could not be prepared.";
    const key = success
      ? `success:${successMessage}`
      : error
        ? `error:${errorMessage}`
        : "";
    if (!shouldShowServiceToast(key)) return;
    if (success) toast.success(successMessage);
    if (error) toast.error(errorMessage);
  });

  return (
    <Card class="w-full overflow-hidden">
      <CardHeader class="gap-4 sm:flex-row sm:items-start sm:justify-between">
        <div class="max-w-3xl">
          <div class="mb-3 h-px w-12 bg-foreground" />
          <h1 class="text-xl font-semibold tracking-tight">System</h1>
          <CardDescription>
            Network, software updates, and device safety
          </CardDescription>
        </div>
        <Flex class="w-auto flex-wrap gap-2">
          <Button as="a" href="#/devices" variant="outline" size="sm">
            View devices
          </Button>
          <Button as="a" href="#/devices/add" size="sm">
            Add device
          </Button>
        </Flex>
      </CardHeader>
      <CardContent class="flex flex-col gap-5">
        <Grid cols={1} colsLg={2} class="gap-5">
          <Card class="h-full">
            <CardHeader>
              <CardTitle>Wi‑Fi network</CardTitle>
              <CardDescription>Current local network settings</CardDescription>
            </CardHeader>
            <CardContent>
              <StatusGrid
                items={[
                  {
                    label: "Network",
                    value: activeNetworkName(),
                    tone: network().station_ipv4 ? "ok" : "warn",
                  },
                  {
                    label: "Mode",
                    value: activeNetworkMode(),
                    tone: network().station_ipv4 ? "ok" : "muted",
                  },
                  {
                    label: "IP address",
                    value: activeNetworkAddress(),
                    tone: "muted",
                  },
                  {
                    label: "Device name",
                    value: system().hostname,
                    tone: "muted",
                  },
                ]}
              />
            </CardContent>
            <CardFooter class="justify-end">
              <Button type="button" onClick={openWifiSheet}>
                Edit Wi‑Fi
              </Button>
            </CardFooter>
          </Card>
          <Sheet
            open={wifiSheetOpen()}
            onOpenChange={(open) =>
              open ? setWifiSheetOpen(true) : closeWifiSheet()
            }
          >
            <SheetContent
              position="right"
              class="flex w-full max-w-none flex-col gap-0 overflow-hidden! p-0 sm:max-w-lg"
            >
              <SheetHeader class="shrink-0 border-b px-6 py-5 pr-12">
                <SheetTitle>Wi‑Fi network</SheetTitle>
                <SheetDescription>
                  Choose a nearby network or enter hidden-network details.
                </SheetDescription>
              </SheetHeader>
              <form class="flex min-h-0 flex-1 flex-col" onSubmit={submitWifi}>
                <div class="min-h-0 flex-1 space-y-5 overflow-y-auto px-6 py-5">
                  <Stepper
                    steps={[
                      { label: "Network", icon: HiOutlineWifi },
                      { label: "Credentials", icon: HiOutlineDocumentText },
                      { label: "Review", icon: HiOutlineCheck },
                    ]}
                    activeStep={wifiSetupStep()}
                    maxReachableStep={wifiForm.values().ssid.trim() ? 2 : 0}
                    onStepChange={setWifiSetupStep}
                    aria-label="Wi-Fi setup steps"
                  />
                  <Show when={wifiSetupStep() === 0}>
                    <div class="flex flex-col gap-5">
                      <div class="space-y-3">
                        <div>
                          <h3 class="text-sm font-semibold">Saved networks</h3>
                          <p class="text-sm text-muted-foreground">
                            {savedWifiResult.data?.detail ??
                              (savedWifiResult.isPending
                                ? "Loading saved networks…"
                                : "Saved networks are unavailable.")}
                          </p>
                          <p class="text-xs text-muted-foreground">
                            Up to 8 encrypted networks are kept. On startup,
                            Core Pilot tries saved networks in this order before
                            opening its setup access point.
                          </p>
                        </div>
                        <Show
                          when={savedWifiNetworks().length > 0}
                          fallback={
                            <Alert>
                              <p>No Wi‑Fi networks are saved.</p>
                            </Alert>
                          }
                        >
                          <Grid class="gap-2" aria-label="Saved Wi-Fi networks">
                            <For each={savedWifiNetworks()}>
                              {(item) => (
                                <div class="flex min-w-0 items-start gap-2 rounded-md border p-3 sm:items-center sm:gap-3">
                                  <HiOutlineWifi
                                    class="shrink-0 text-muted-foreground"
                                    aria-hidden="true"
                                  />
                                  <div class="min-w-0 flex-1">
                                    <div class="flex min-w-0 flex-wrap items-center gap-1.5">
                                      <span class="truncate text-sm font-semibold">
                                        {item.ssid}
                                      </span>
                                      <Badge>
                                        {item.connected ? "Connected" : "Saved"}
                                      </Badge>
                                    </div>
                                  </div>
                                  <ConfirmDestructiveAction
                                    triggerLabel="Forget"
                                    triggerAriaLabel={`Forget ${item.ssid}`}
                                    title={`Forget ${item.ssid}?`}
                                    description={
                                      item.connected
                                        ? "Core Pilot will remove the credentials for the connected network and try another saved network. If none connects, this dashboard will disconnect and the setup access point will start."
                                        : "Core Pilot will remove the saved credentials for this network. You will need to enter its password to connect again."
                                    }
                                    confirmLabel="Forget network"
                                    disabled={wifiForget.isPending}
                                    onConfirm={() => forgetSavedWifi(item.ssid)}
                                  />
                                </div>
                              )}
                            </For>
                          </Grid>
                        </Show>
                      </div>
                      <fieldset class="flex flex-col gap-4">
                        <Flex alignItems="start" class="flex-wrap gap-3">
                          <div class="min-w-0">
                            <legend class="text-sm font-semibold">
                              Nearby networks
                            </legend>
                            <p class="text-sm text-muted-foreground">
                              {wifiScanState().detail}
                            </p>
                          </div>
                          <Button
                            type="button"
                            variant="outline"
                            size="sm"
                            disabled={
                              wifiScanLoading() || wifiScanState().scanning
                            }
                            onClick={() => void refreshWifiScan()}
                          >
                            <Show
                              when={
                                wifiScanLoading() || wifiScanState().scanning
                              }
                            >
                              <Spinner />
                            </Show>
                            {wifiScanState().scan_state === "queued"
                              ? "Starting…"
                              : wifiScanLoading() || wifiScanState().scanning
                                ? "Scanning…"
                                : "Refresh"}
                          </Button>
                        </Flex>
                        <OperationStatus
                          title={
                            wifiScanState().scan_state === "queued"
                              ? "Wi-Fi scan queued"
                              : wifiScanState().scan_state === "scanning"
                                ? "Scanning nearby networks"
                                : wifiScanState().scan_state === "error"
                                  ? "Wi-Fi scan needs attention"
                                  : "Wi-Fi scan ready"
                          }
                          description={wifiScanState().detail}
                          active={wifiScanLoading() || wifiScanState().scanning}
                          trailing={
                            wifiScanState().scan_state === "error" ? (
                              <Button
                                type="button"
                                size="sm"
                                variant="outline"
                                onClick={() => void refreshWifiScan()}
                              >
                                Retry
                              </Button>
                            ) : undefined
                          }
                        />
                        <Show
                          when={wifiScanState().networks.length > 0}
                          fallback={
                            <Show
                              when={
                                wifiScanLoading() ||
                                ["queued", "scanning"].includes(
                                  wifiScanState().scan_state,
                                )
                              }
                              fallback={
                                <Alert>
                                  <p>
                                    No nearby networks have been reported yet.
                                    Refresh, or enter the SSID manually below.
                                  </p>
                                </Alert>
                              }
                            >
                              <Grid
                                class="gap-2"
                                aria-busy="true"
                                aria-live="polite"
                              >
                                <For each={Array.from({ length: 3 })}>
                                  {(_, index) => (
                                    <div class="flex items-center gap-2 rounded-md border p-3">
                                      <Skeleton class="size-4 shrink-0 rounded-full" />
                                      <Skeleton class="size-3 shrink-0 rounded-full" />
                                      <div class="min-w-0 flex-1 space-y-2">
                                        <Skeleton
                                          class={
                                            index() === 1
                                              ? "h-4 w-2/3"
                                              : "h-4 w-1/2"
                                          }
                                        />
                                        <Skeleton class="h-3 w-full max-w-80" />
                                      </div>
                                    </div>
                                  )}
                                </For>
                              </Grid>
                            </Show>
                          }
                        >
                          <Grid
                            class="scroll-fade-y max-h-72 gap-2 overflow-y-auto"
                            role="radiogroup"
                            aria-label="Nearby Wi-Fi networks"
                          >
                            <For each={wifiScanState().networks}>
                              {(item) => (
                                <label class="relative flex cursor-pointer items-start gap-2 rounded-md border p-3">
                                  <input
                                    type="radio"
                                    name="wifi-network"
                                    value={item.id}
                                    checked={
                                      selectedWifiNetworkId() === item.id
                                    }
                                    disabled={isConnectedWifiNetwork(item)}
                                    aria-label={
                                      isConnectedWifiNetwork(item)
                                        ? `${item.ssid} is already connected`
                                        : `Select ${item.ssid}`
                                    }
                                    onChange={() => selectWifiNetwork(item)}
                                  />
                                  <span
                                    class={wifiSignalTone(item.signal_dbm)}
                                    aria-hidden="true"
                                  >
                                    ●
                                  </span>
                                  <span class="min-w-0 flex-1 text-sm">
                                    <span class="flex min-w-0 flex-wrap items-center gap-1.5">
                                      <span class="truncate font-semibold">
                                        {item.ssid}
                                      </span>
                                      <Show when={item.secured}>
                                        <span class="inline-flex items-center gap-1 text-xs text-muted-foreground">
                                          <HiOutlineLockClosed aria-hidden="true" />
                                          <span class="sr-only">Secured</span>
                                        </span>
                                      </Show>
                                      <Show
                                        when={isConnectedWifiNetwork(item)}
                                        fallback={
                                          <Show
                                            when={isSavedWifiNetwork(item.ssid)}
                                          >
                                            <Badge>Saved</Badge>
                                          </Show>
                                        }
                                      >
                                        <Badge>Connected</Badge>
                                      </Show>
                                    </span>
                                    <span class="block wrap-anywhere text-xs text-muted-foreground">
                                      {wifiSignalLabel(item.signal_dbm)} ·{" "}
                                      {item.signal_dbm} dBm · Ch {item.channel}{" "}
                                      · {item.auth} · {item.bssid}
                                    </span>
                                  </span>
                                </label>
                              )}
                            </For>
                          </Grid>
                        </Show>
                      </fieldset>
                    </div>
                  </Show>
                  <Show when={wifiSetupStep() === 1}>
                    <div class="flex flex-col gap-4">
                      <TextField
                        required
                        validationState={
                          wifiForm.errors.ssid ? "invalid" : "valid"
                        }
                      >
                        <TextFieldLabel for="wifi-ssid">
                          Selected network
                        </TextFieldLabel>
                        <TextFieldInput
                          {...wifiForm.register("ssid", {
                            validate: (value) => {
                              const result =
                                wifiSchema.shape.ssid.safeParse(value);
                              return (
                                result.success ||
                                result.error.issues[0]?.message
                              );
                            },
                          })}
                          id="wifi-ssid"
                          value={wifiForm.values().ssid}
                          maxLength={32}
                          autocomplete="off"
                          placeholder="Select a network or enter SSID"
                        />
                        <TextFieldDescription>
                          {selectedWifiNetwork()
                            ? "Selected from nearby Wi‑Fi scan."
                            : "Manual SSID fallback for hidden networks."}
                        </TextFieldDescription>
                        <Show when={wifiForm.errors.ssid?.message}>
                          {(message) => (
                            <TextFieldErrorMessage>
                              {message()}
                            </TextFieldErrorMessage>
                          )}
                        </Show>
                      </TextField>
                      <TextField
                        validationState={
                          wifiForm.errors.password ? "invalid" : "valid"
                        }
                      >
                        <TextFieldLabel for="wifi-password">
                          Password
                        </TextFieldLabel>
                        <TextFieldInput
                          {...wifiForm.register("password", {
                            validate: (value) => {
                              const result =
                                wifiSchema.shape.password.safeParse(value);
                              return (
                                result.success ||
                                result.error.issues[0]?.message
                              );
                            },
                          })}
                          id="wifi-password"
                          type="password"
                          value={wifiForm.values().password}
                          maxLength={63}
                          autocomplete="new-password"
                        />
                        <TextFieldDescription>
                          {selectedWifiNetwork()?.secured === false
                            ? "Selected network is open; no password is required."
                            : "Password for the selected Wi‑Fi network."}
                        </TextFieldDescription>
                        <Show when={wifiForm.errors.password?.message}>
                          {(message) => (
                            <TextFieldErrorMessage>
                              {message()}
                            </TextFieldErrorMessage>
                          )}
                        </Show>
                      </TextField>
                      <TextField
                        validationState={
                          wifiForm.errors.hostname ? "invalid" : "valid"
                        }
                      >
                        <TextFieldLabel for="device-hostname">
                          Device name
                        </TextFieldLabel>
                        <TextFieldInput
                          {...wifiForm.register("hostname", {
                            validate: (value) => {
                              const result =
                                wifiSchema.shape.hostname.safeParse(value);
                              return (
                                result.success ||
                                result.error.issues[0]?.message
                              );
                            },
                          })}
                          id="device-hostname"
                          value={wifiForm.values().hostname}
                          maxLength={63}
                        />
                        <TextFieldDescription>
                          Shown on your local network as corepilot.local.
                        </TextFieldDescription>
                        <Show when={wifiForm.errors.hostname?.message}>
                          {(message) => (
                            <TextFieldErrorMessage>
                              {message()}
                            </TextFieldErrorMessage>
                          )}
                        </Show>
                      </TextField>
                    </div>
                  </Show>
                  <Show when={wifiSetupStep() === 2}>
                    <Card class="border-dashed">
                      <CardHeader>
                        <CardTitle>Review and reconnect</CardTitle>
                        <CardDescription>
                          Core Pilot will save the selected network and may
                          disconnect this dashboard while it joins it.
                        </CardDescription>
                      </CardHeader>
                      <CardContent class="space-y-2 text-sm">
                        <p>
                          <span class="text-muted-foreground">Network: </span>
                          {wifiForm.values().ssid || "Not selected"}
                        </p>
                        <p>
                          <span class="text-muted-foreground">
                            Device name:{" "}
                          </span>
                          {wifiForm.values().hostname || system().hostname}
                        </p>
                        <p class="text-muted-foreground">
                          After saving, reconnect to the dashboard at the new
                          local address shown on this page. Your password is
                          never displayed.
                        </p>
                      </CardContent>
                    </Card>
                  </Show>
                </div>
                <SheetFooter class="shrink-0 gap-2 border-t px-6 py-4">
                  <Button
                    type="button"
                    variant="outline"
                    onClick={closeWifiSheet}
                  >
                    Cancel
                  </Button>
                  <Show
                    when={wifiSetupStep() < 2}
                    fallback={
                      <Button type="submit" disabled={wifiMutation.isPending}>
                        <Show when={wifiMutation.isPending}>
                          <Spinner />
                        </Show>
                        Save Wi‑Fi
                      </Button>
                    }
                  >
                    <Button
                      type="button"
                      disabled={
                        wifiSetupStep() === 0 && !wifiForm.values().ssid.trim()
                      }
                      onClick={() =>
                        setWifiSetupStep((step) => Math.min(step + 1, 2))
                      }
                    >
                      Continue
                    </Button>
                  </Show>
                </SheetFooter>
              </form>
            </SheetContent>
          </Sheet>

          <Card class="h-full">
            <CardHeader>
              <CardTitle>Time source</CardTitle>
              <CardDescription>
                Current clock and synchronization settings
              </CardDescription>
            </CardHeader>
            <CardContent>
              <StatusGrid
                items={[
                  {
                    label: "Current time",
                    value: displayedCurrentTime() ?? "syncing",
                    tone: displayedCurrentTime() ? "ok" : "warn",
                  },
                  {
                    label: "Current date",
                    value: displayedCurrentDate() ?? "pending",
                    tone: displayedCurrentDate() ? "ok" : "warn",
                  },
                  {
                    label: "NTP server",
                    value: activeTimeSource(),
                    tone: time().ntp === "synced" ? "ok" : "muted",
                  },
                  {
                    label: "Timezone",
                    value: time().timezone_label,
                    tone: "muted",
                  },
                ]}
              />
            </CardContent>
            <CardFooter class="justify-end">
              <Button
                type="button"
                onClick={() => {
                  resetTimeForm();
                  setTimeSheetOpen(true);
                }}
              >
                Edit time settings
              </Button>
            </CardFooter>
          </Card>
          <Sheet
            open={timeSheetOpen()}
            onOpenChange={(open) =>
              open ? setTimeSheetOpen(true) : closeTimeSheet()
            }
          >
            <SheetContent
              position="right"
              class="flex w-full max-w-none flex-col gap-0 overflow-hidden! p-0 sm:max-w-lg"
            >
              <SheetHeader class="shrink-0 border-b px-6 py-5 pr-12">
                <SheetTitle>Time source</SheetTitle>
                <SheetDescription>
                  Search approved time sources and choose your local timezone.
                </SheetDescription>
              </SheetHeader>
              <form
                class="flex min-h-0 flex-1 flex-col"
                onSubmit={saveTimeConfig}
              >
                <div class="min-h-0 flex-1 space-y-5 overflow-y-auto px-6 py-5">
                  <Grid cols={1} colsSm={2} class="gap-4">
                    <div class="min-w-0 space-y-1.5">
                      <input
                        {...timeForm.register("ntpServerId")}
                        type="hidden"
                        value={timeForm.values().ntpServerId}
                      />
                      <label for="ntp-server" class="text-sm font-medium">
                        NTP server
                      </label>
                      <Combobox<string>
                        options={ntpOptions.map((option) => option.label)}
                        open={ntpComboboxOpen()}
                        onOpenChange={setNtpComboboxOpen}
                        closeOnSelection
                        value={
                          ntpOptions.find(
                            (option) =>
                              option.value === timeForm.values().ntpServerId,
                          )?.label
                        }
                        onChange={(label) => {
                          const option = ntpOptions.find(
                            (candidate) => candidate.label === label,
                          );
                          if (!option) return;
                          timeForm.setValue("ntpServerId", option.value);
                          setNtpComboboxOpen(false);
                        }}
                        placeholder="Search time sources…"
                        itemComponent={(props) => (
                          <ComboboxItem
                            item={props.item}
                            onClick={() => setNtpComboboxOpen(false)}
                          >
                            <ComboboxItemLabel>
                              {props.item.rawValue}
                            </ComboboxItemLabel>
                            <ComboboxItemIndicator />
                          </ComboboxItem>
                        )}
                      >
                        <ComboboxControl aria-label="NTP server">
                          <ComboboxInput id="ntp-server" />
                          <ComboboxTrigger />
                        </ComboboxControl>
                        <ComboboxContent />
                      </Combobox>
                      <Show when={timeForm.errors.ntpServerId?.message}>
                        {(message) => (
                          <p class="text-sm text-error" role="alert">
                            {message()}
                          </p>
                        )}
                      </Show>
                    </div>
                    <div class="min-w-0 space-y-1.5">
                      <input
                        {...timeForm.register("timezoneId")}
                        type="hidden"
                        value={timeForm.values().timezoneId}
                      />
                      <label for="timezone" class="text-sm font-medium">
                        Timezone
                      </label>
                      <Combobox<string>
                        options={timezoneOptions().map(
                          (option) => option.label,
                        )}
                        open={timezoneComboboxOpen()}
                        onOpenChange={setTimezoneComboboxOpen}
                        closeOnSelection
                        value={
                          timezoneOptions().find(
                            (option) =>
                              option.value === timeForm.values().timezoneId,
                          )?.label
                        }
                        onChange={(label) => {
                          const option = timezoneOptions().find(
                            (candidate) => candidate.label === label,
                          );
                          if (!option) return;
                          timeForm.setValue("timezoneId", option.value);
                          setTimezoneComboboxOpen(false);
                        }}
                        placeholder="Search timezones…"
                        itemComponent={(props) => (
                          <ComboboxItem
                            item={props.item}
                            onClick={() => setTimezoneComboboxOpen(false)}
                          >
                            <ComboboxItemLabel>
                              {props.item.rawValue}
                            </ComboboxItemLabel>
                            <ComboboxItemIndicator />
                          </ComboboxItem>
                        )}
                      >
                        <ComboboxControl aria-label="Timezone">
                          <ComboboxInput id="timezone" />
                          <ComboboxTrigger />
                        </ComboboxControl>
                        <ComboboxContent class="max-h-72 overflow-y-auto" />
                      </Combobox>
                      <Show when={timeForm.errors.timezoneId?.message}>
                        {(message) => (
                          <p class="text-sm text-error" role="alert">
                            {message()}
                          </p>
                        )}
                      </Show>
                    </div>
                  </Grid>
                  <StatusGrid
                    items={[
                      {
                        label: "Current time",
                        value: displayedCurrentTime() ?? "syncing",
                        tone: displayedCurrentTime() ? "ok" : "warn",
                      },
                      {
                        label: "Current date",
                        value: displayedCurrentDate() ?? "pending",
                        tone: displayedCurrentDate() ? "ok" : "warn",
                      },
                      {
                        label: "Timezone",
                        value: time().timezone_label,
                        tone: "muted",
                      },
                    ]}
                  />
                </div>
                <SheetFooter class="shrink-0 gap-2 border-t px-6 py-4">
                  <Button
                    type="button"
                    variant="outline"
                    onClick={closeTimeSheet}
                  >
                    Cancel
                  </Button>
                  <Button type="submit" disabled={timeMutation.isPending}>
                    <Show when={timeMutation.isPending}>
                      <Spinner />
                    </Show>
                    Save time settings
                  </Button>
                </SheetFooter>
              </form>
            </SheetContent>
          </Sheet>

          <form class="lg:col-span-2" onSubmit={submitOta}>
            <Card>
              <CardHeader>
                <CardTitle>Flash new firmware</CardTitle>
                <CardDescription>
                  Upload a Core Pilot application image to the inactive firmware
                  slot.
                </CardDescription>
              </CardHeader>
              <CardContent class="flex flex-col gap-5">
                <Show
                  when={
                    preparingDigest() ||
                    otaUploadVisible() ||
                    firmwareOtaInProgress() ||
                    firmwareOtaNeedsReboot() ||
                    Boolean(otaClientError())
                  }
                >
                  <OperationStatus
                    title={
                      otaClientError()
                        ? "Firmware update needs attention"
                        : preparingDigest()
                          ? "Verifying firmware image"
                          : firmwareOtaNeedsReboot()
                            ? "Firmware ready to activate"
                            : otaStage()
                    }
                    description={otaClientError() || system().ota}
                    current={
                      preparingDigest()
                        ? digestProgressBytes()
                        : otaUploadVisible()
                          ? otaProgressBytes()
                          : system().ota_progress_bytes
                    }
                    total={
                      otaUploadVisible() || preparingDigest()
                        ? otaProgressTotal()
                        : system().ota_total_bytes
                    }
                    unit="bytes"
                    active={
                      preparingDigest() ||
                      otaMutation.isPending ||
                      otaChunk.isPending ||
                      firmwareOtaInProgress()
                    }
                    trailing={
                      otaClientError() && !otaMutation.isPending ? (
                        <Button
                          type="button"
                          size="sm"
                          variant="outline"
                          onClick={clearFirmware}
                        >
                          Choose another image
                        </Button>
                      ) : undefined
                    }
                  />
                </Show>
                <fieldset class="flex flex-col gap-4">
                  <legend class="sr-only">Firmware image</legend>
                  <input
                    {...otaForm.register("file", {
                      validate: (value) => {
                        const result = otaSchema.shape.file.safeParse(value);
                        return (
                          result.success || result.error.issues[0]?.message
                        );
                      },
                    })}
                    id="firmware-file"
                    class="sr-only"
                    type="file"
                    accept=".bin,application/octet-stream"
                    onChange={(event) => void selectFirmware(event)}
                  />
                  <label
                    for="firmware-file"
                    class="flex min-h-32 cursor-pointer flex-col items-center justify-center gap-2 rounded-lg border border-dashed border-input bg-muted/30 px-4 py-6 text-center transition-colors hover:bg-muted/60 focus-within:ring-2 focus-within:ring-ring"
                  >
                    <span
                      class="grid size-10 place-items-center rounded-full border bg-background"
                      aria-hidden="true"
                    >
                      <HiOutlineArrowUpTray class="size-5" />
                    </span>
                    <span class="text-sm font-semibold">
                      Choose application firmware
                    </span>
                    <span class="text-xs text-muted-foreground">
                      App .bin only · up to {formatOtaSize(MAX_OTA_IMAGE_BYTES)}
                    </span>
                  </label>
                  <Show when={otaForm.errors.file?.message}>
                    {(message) => (
                      <p class="text-sm text-error" role="alert">
                        {message()}
                      </p>
                    )}
                  </Show>
                  <Show when={otaForm.values().file}>
                    <Flex
                      class="gap-3 rounded-lg border bg-muted/20 p-3"
                      alignItems="start"
                    >
                      <span
                        class="grid size-9 shrink-0 place-items-center rounded-md bg-muted"
                        aria-hidden="true"
                      >
                        <HiOutlineDocumentText class="size-5" />
                      </span>
                      <div class="min-w-0 flex-1">
                        <p class="truncate text-sm font-medium">
                          {otaForm.values().file?.name}
                        </p>
                        <p class="text-xs text-muted-foreground">
                          {otaSizeLabel()} · {otaStage()}
                        </p>
                      </div>
                      <Show
                        when={!otaMutation.isPending && !otaChunk.isPending}
                      >
                        <TooltipIconButton
                          type="button"
                          size="icon"
                          variant="ghost"
                          aria-label="Remove selected firmware"
                          onClick={clearFirmware}
                        >
                          <HiOutlineTrash aria-hidden="true" />
                        </TooltipIconButton>
                      </Show>
                    </Flex>
                  </Show>
                  <Show when={otaForm.values().file}>
                    <Grid
                      cols={1}
                      colsSm={2}
                      class="gap-3 rounded-md border p-3"
                    >
                      <div>
                        <p class="text-sm font-medium">SHA‑256</p>
                        <Flex class="min-w-0 gap-2">
                          <code
                            class="min-w-0 flex-1 truncate text-xs"
                            title={otaSha256()}
                          >
                            {digestLabel()}
                          </code>
                          <Button
                            type="button"
                            variant="outline"
                            size="sm"
                            class="h-8 shrink-0 px-2 text-xs"
                            aria-label="Copy SHA‑256 digest"
                            disabled={!otaSha256()}
                            onClick={() => void copyOtaSha256()}
                          >
                            Copy
                          </Button>
                        </Flex>
                      </div>
                      <div>
                        <p class="text-sm font-medium">Version</p>
                        <span class="text-sm font-medium">
                          {otaVersion() || "Not set"}
                        </span>
                      </div>
                    </Grid>
                  </Show>
                  <Show
                    when={otaUploadVisible() || preparingDigest()}
                    fallback={
                      <Show
                        when={
                          firmwareOtaInProgress() || firmwareOtaNeedsReboot()
                        }
                      >
                        <div
                          class="flex flex-col gap-2 rounded-lg border p-3 text-xs text-muted-foreground"
                          aria-live="polite"
                          aria-busy={firmwareOtaInProgress()}
                        >
                          <Flex>
                            <span class="font-medium text-foreground">
                              Firmware status
                            </span>
                            <span>{firmwareOtaProgressPercent()}%</span>
                          </Flex>
                          <Progress
                            value={firmwareOtaProgressPercent()}
                            minValue={0}
                            maxValue={100}
                          />
                          <span>{system().ota}</span>
                        </div>
                      </Show>
                    }
                  >
                    <div
                      class="flex flex-col gap-2 rounded-lg border p-3 text-xs text-muted-foreground"
                      aria-live="polite"
                      aria-busy="true"
                    >
                      <Flex>
                        <span class="font-medium text-foreground">
                          {preparingDigest()
                            ? "Calculating digest"
                            : "Firmware upload"}
                        </span>
                        <span>{otaProgressPercent()}%</span>
                      </Flex>
                      <Progress
                        value={otaProgressPercent()}
                        minValue={0}
                        maxValue={100}
                      />
                      <span>
                        {preparingDigest()
                          ? `${digestProgressBytes().toLocaleString()} / ${otaProgressTotal().toLocaleString()} bytes hashed`
                          : `${otaProgressBytes().toLocaleString()} / ${otaProgressTotal().toLocaleString()} bytes staged`}
                      </span>
                    </div>
                  </Show>
                  <Show when={otaClientError()}>
                    <Alert variant="destructive">
                      <p>{otaClientError()}</p>
                    </Alert>
                  </Show>
                </fieldset>
                <Switch
                  checked={otaForm.values().restartAfterUpdate}
                  onChange={(checked) =>
                    otaForm.setValue("restartAfterUpdate", checked)
                  }
                  class="flex items-center gap-3 rounded-lg border bg-muted/20 px-4 py-3"
                >
                  <div class="min-w-0 flex-1">
                    <SwitchLabel>Restart after update</SwitchLabel>
                    <p class="mt-1 text-xs text-muted-foreground">
                      Apply the new image immediately after verification.
                    </p>
                  </div>
                  <SwitchControl>
                    <SwitchThumb />
                  </SwitchControl>
                </Switch>
              </CardContent>
              <CardFooter class="justify-end">
                <Button
                  type="submit"
                  disabled={
                    otaMutation.isPending ||
                    otaChunk.isPending ||
                    preparingDigest() ||
                    !otaForm.values().file ||
                    !otaSha256() ||
                    Boolean(otaUploadDisabledReason())
                  }
                >
                  <Show
                    when={
                      otaMutation.isPending ||
                      otaChunk.isPending ||
                      preparingDigest()
                    }
                  >
                    <Spinner />
                  </Show>
                  {otaMutation.isPending
                    ? "Validating…"
                    : otaChunk.isPending
                      ? "Uploading…"
                      : "Validate & upload"}
                </Button>
              </CardFooter>
            </Card>
          </form>
        </Grid>
      </CardContent>
    </Card>
  );
}
