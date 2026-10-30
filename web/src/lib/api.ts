import { authHeaders, clearAccess } from "./auth";
import { MAX_OTA_IMAGE_BYTES } from "./ota-limits";
import { z } from "zod";

import { translateFirmwareError } from "~/lib/errorCodes";
import {
  activityPageRequestSchema,
  aiChatRequestSchema,
  aiMemoryRequestSchema,
  aiPromptRequestSchema,
  aiProviderRequestSchema,
  automationUpsertRequestSchema,
  groupUpsertRequestSchema,
  matterCommissionCancelRequestSchema,
  matterCommissionRequestSchema,
  matterDeviceForgetRequestSchema,
  matterDeviceMutationSchema,
  matterDeviceRemoveRequestSchema,
  mcpInvokeRequestSchema,
  mcpToolPermissionRequestSchema,
  modelDeleteRequestSchema,
  otaChunkRequestSchema,
  otaPrepareRequestSchema,
  rebootRequestSchema,
  roomUpsertRequestSchema,
  sceneUpsertRequestSchema,
  smtpConfigRequestSchema,
  smtpTestRequestSchema,
  threadDatasetUpdateRequestSchema,
  timeConfigRequestSchema,
  wifiForgetRequestSchema,
  wifiProvisionRequestSchema,
  wifiScanPageRequestSchema,
  wifiScanRefreshRequestSchema,
} from "~/lib/request-contracts";

export type Tone = "ok" | "warn" | "error" | "muted";

export type Health = {
  product: string;
  hostname: string;
  storage_ok: boolean;
  ai_enabled: boolean;
  auth: string;
};

export type NetworkStatus = {
  mode: string;
  setup_ssid: string;
  station_ssid?: string | null;
  station_bssid?: string | null;
  ipv4: string;
  station_ipv4?: string | null;
  hostname: string;
  dhcp: string;
  http: string;
  wan_enabled: boolean;
  cloud_relay_enabled: boolean;
};

export type WifiNetwork = {
  id: string;
  ssid: string;
  bssid: string;
  connected: boolean;
  signal_dbm: number;
  channel: number;
  secured: boolean;
  auth: string;
};

export type WifiScan = {
  networks: WifiNetwork[];
  scanning: boolean;
  scan_state: string;
  detail: string;
};

export type SavedWifiNetwork = {
  ssid: string;
  connected: boolean;
};

export type SavedWifiNetworks = {
  networks: SavedWifiNetwork[];
  detail: string;
};

export type WifiScanPageRequest = {
  last_seen?: string | null;
};

export type WifiScanPage = {
  networks: WifiNetwork[];
  scanning: boolean;
  scan_state: string;
  next_last_seen?: string | null;
  detail: string;
};

export type ActivityPageRequest = {
  before_sequence?: number | null;
  limit?: number;
};

export type ActivityEvent = {
  sequence: number;
  unix_seconds: number | null;
  uptime_seconds: number;
  kind: string;
  detail: string;
};

export type ActivityPage = {
  events: ActivityEvent[];
  next_before_sequence: number | null;
  retained: number;
  dropped_events: number;
  detail: string;
};

const activityPageResponseSchema: z.ZodType<ActivityPage> = z
  .object({
    events: z.array(
      z
        .object({
          sequence: z.number().int().min(0).max(0xffffffff),
          unix_seconds: z.number().int().min(0).max(0xffffffff).nullable(),
          uptime_seconds: z.number().int().min(0).max(0xffffffff),
          kind: z.string(),
          detail: z.string(),
        })
        .strict(),
    ),
    next_before_sequence: z.number().int().min(0).max(0xffffffff).nullable(),
    retained: z.number().int().min(0).max(0xffff),
    dropped_events: z.number().int().min(0).max(0xffffffff),
    detail: z.string(),
  })
  .strict();

export type NtpServerOption = {
  id: string;
  label: string;
  host: string;
};

export const NTP_SERVER_OPTIONS: NtpServerOption[] = [
  {
    id: "cloudflare",
    label: "Cloudflare time.cloudflare.com",
    host: "time.cloudflare.com",
  },
  {
    id: "google",
    label: "Google time.google.com",
    host: "time.google.com",
  },
  {
    id: "ntp_pool",
    label: "NTP Pool pool.ntp.org",
    host: "pool.ntp.org",
  },
];

export type TimeStatus = {
  ntp_server_id: string;
  ntp_server_host: string;
  timezone_id: string;
  timezone_label: string;
  timezone_offset_minutes: number;
  timezone_offset_label: string;
  dns: string;
  ntp: string;
  rtc: string;
  last_sync_unix_seconds?: number | null;
  current_unix_seconds?: number | null;
  current_date?: string | null;
  current_time?: string | null;
  detail: string;
};

export type TimeConfigRequest = {
  ntp_server_id: string;
  timezone_id: string;
  timezone_offset_minutes: number;
};

export type StorageStatus = {
  state: string;
  sd_root: string;
  encrypted: boolean;
  mutations_allowed: boolean;
  device_seed_exportable: boolean;
  detail: string;
};

export type MatterTransport = "wifi" | "thread";

export type MatterCommissioningPhase =
  | "queued"
  | "discovery"
  | "pase"
  | "cancelling"
  | "attestation"
  | "csr_add_noc"
  | "network_configuration"
  | "network_connect"
  | "commissioning_complete"
  | "operational_discovery"
  | "case"
  | "subscriptions"
  | "persistence"
  | "completed"
  | "failed"
  | "cancelled";

export type MatterOperation = {
  id: number;
  transport: MatterTransport;
  phase: MatterCommissioningPhase;
  phase_retries: number;
  phase_timeout_ms: number | null;
  failure: { code: string; detail: string } | null;
};

export type MatterRuntimeSnapshot = {
  active: MatterOperation | null;
  pending: MatterOperation | null;
  last_terminal: MatterOperation | null;
};

export type MatterStatus = {
  implementation: string;
  commissioned: boolean;
  fabric_configured: boolean;
  subscriptions_active: number;
  ipv6_ready: boolean;
  mdns_ready: boolean;
  dns_sd_ready: boolean;
  thread_ready: boolean;
  thread_dataset_configured: boolean;
  wifi_commissioning_ready: boolean;
  thread_commissioning_ready: boolean;
  adapter_status: string;
  parser_ready: boolean;
  transport_adapter_ready: boolean;
  fabric_credentials_ready: boolean;
  pase_ready: boolean;
  case_ready: boolean;
  subscriptions_ready: boolean;
  operation: MatterRuntimeSnapshot;
  detail: string;
};

export type MatterCommissionRequest = {
  transport: MatterTransport;
  setup_payload: string;
  thread_dataset_id?: string;
};

export type MatterCommissionResponse = {
  accepted: boolean;
  operation_id: number | null;
  transport: MatterTransport;
  status: string;
  detail: string;
};

export type ThreadDatasetUpdateRequest = {
  active_dataset_tlvs_hex: string;
};

export type ThreadDatasetUpdateResponse = {
  accepted: boolean;
  status: string;
  detail: string;
};

export type MatterCommissionCancelResponse = {
  accepted: boolean;
  status: string;
  detail: string;
};

export type SmtpSecurityMode = "start_tls" | "tls" | "plain_lan_only";

export type SmtpConfig = {
  enabled: boolean;
  host: string;
  port: number;
  security: SmtpSecurityMode;
  username: string;
  password?: string | null;
  from: string;
  recipients: string[];
};

export type SmtpStatus = {
  enabled: boolean;
  configured: boolean;
  host: string;
  port: number;
  security: SmtpSecurityMode;
  username_configured: boolean;
  password_configured: boolean;
  recipients: number;
  transport_ready: boolean;
  detail: string;
};

export type SmtpConfigRequest = {
  config: SmtpConfig;
};

export type SmtpMessage = {
  subject: string;
  body: string;
  recipients: string[];
};

export type SmtpTestRequest = {
  message: SmtpMessage;
};

export type ProviderTransportStatus = {
  http_ipv4_literal: boolean;
  dns: boolean;
  tls: boolean;
  websockets: boolean;
  certificate_validation: boolean;
  detail: string;
};

export type AiStatus = {
  enabled: boolean;
  provider_configured: boolean;
  mcp_enabled: boolean;
  max_tool_rounds: number;
  max_repeated_tool_calls: number;
  max_actions: number;
  destructive_confirmation: string;
  persistence: string;
  provider_transport: ProviderTransportStatus;
  provider_transport_requirements: string[];
};

export type MutationResponse = {
  accepted: boolean;
  status: string;
  detail: string;
};

/** Converts a firmware-level rejection into a mutation error. */
export function requireAccepted<
  T extends { accepted: boolean; detail?: string; reply?: string },
>(response: T): T {
  if (!response.accepted) {
    throw new Error(
      response.detail || response.reply || "Core Pilot rejected the request.",
    );
  }
  return response;
}

export type AiChatRole = "user" | "assistant" | "tool";

export type AiChatMessage = {
  role: AiChatRole;
  content: string;
};

export type AiChatRequest = {
  message: string;
  history: AiChatMessage[];
  chat_id?: string;
};

export type AiChatResponse = {
  accepted: boolean;
  operation_id: string;
  status: string;
  reply: string;
  actions: { tool: McpToolName; detail: string }[];
};

export type AiMemorySection = {
  id: string;
  title: string;
  markdown: string;
};

export type AiMemoryResponse = {
  sections: AiMemorySection[];
  encrypted: boolean;
  detail: string;
};

export type AiMemoryUpdateRequest = {
  id: string;
  markdown: string;
};

export type AiPromptFile = {
  path: string;
  title: string;
  markdown: string;
  baseline: boolean;
};

export type AiPromptDirectoryResponse = {
  files: AiPromptFile[];
  encrypted: boolean;
  detail: string;
};

export type AiPromptUpdateRequest = {
  path: string;
  markdown: string;
  markdown_base64?: string | null;
};

export type McpContextResponse = {
  system_prompt: string;
  context_json: string;
  tools: McpToolDescriptor[];
};

export type WifiProvisionRequest = {
  credentials: {
    ssid: string;
    password: string;
  };
  hostname?: string;
};

export type WifiForgetRequest = {
  ssid: string;
};

export type AiProviderSetupRequest = {
  provider: {
    backend: "open_ai_compatible" | "anthropic_compatible";
    auth_type: "bearer" | "api_key" | "none";
    base_url: string;
    model: string;
    max_output_tokens: number;
    timeout_ms: number;
    temperature?: number;
    api_key?: string | null;
    api_key_redacted: boolean;
  };
};

export type McpToolName =
  | "ListDevices"
  | "GetDeviceState"
  | "SetOnOff"
  | "SetLevel"
  | "SetColor"
  | "SetColorTemperature"
  | "ListRooms"
  | "CreateRoom"
  | "UpdateRoom"
  | "DeleteRoom"
  | "ListGroups"
  | "CreateGroup"
  | "UpdateGroup"
  | "DeleteGroup"
  | "ActivateScene"
  | "ListScenes"
  | "CreateScene"
  | "UpdateScene"
  | "DeleteScene"
  | "ListAutomations"
  | "CreateAutomation"
  | "UpdateAutomation"
  | "DeleteAutomation"
  | "RunAutomation"
  | "EnableAutomation"
  | "DisableAutomation"
  | "GetHubStatus"
  | "GetMatterOperation"
  | "RemoveMatterDevice";

export type McpToolDescriptor = {
  id: string;
  name: string;
  title: string;
  category: string;
  description: string;
  enabled: boolean;
  destructive: boolean;
  available: boolean;
  inputSchema?: Record<string, unknown>;
};

export type McpToolPermissionUpdateRequest = {
  tool: string;
  enabled: boolean;
  confirmation?: string;
};

export type McpToolsResponse = {
  tools: McpToolDescriptor[];
};

export type McpInvokeRequest = {
  tool: string;
  arguments_json: string;
};

export type McpInvokeResponse = MutationResponse & {
  result?: Record<string, unknown>;
};

export { MAX_OTA_IMAGE_BYTES };

export type OtaPrepareRequest = {
  version: string;
  size_bytes: number;
  sha256_hex: string;
  restart_after_update: boolean;
};

export type OtaChunkRequest = {
  offset: number;
  data_base64: string;
  final_chunk: boolean;
};

export type OtaPhase =
  | "idle"
  | "prepared"
  | "staging"
  | "applying"
  | "activated"
  | "rebooting"
  | "failed";

export type SystemStatus = {
  product: string;
  hostname: string;
  firmware_version: string;
  setup_ap: string;
  display: string;
  storage: string;
  matter: string;
  thread: string;
  ai: string;
  ota: string;
  ota_phase: OtaPhase;
  ota_progress_bytes: number;
  ota_total_bytes: number;
  ota_restart_after_update: boolean;
  recovery: string;
  http_accepted_connections?: number;
  http_active_connections?: number;
  http_connection_errors?: number;
  dashboard_transfers?: number;
  dashboard_last_transfer_ms?: number;
  dashboard_max_transfer_ms?: number;
};

export type Device = {
  id: string | { 0: string };
  name: string;
  room?: string | { 0: string } | null;
  protocol: string;
  capabilities: string[];
  state: unknown[];
  availability: string;
  last_seen: number | { 0: number };
  node_id?: string;
  endpoint_id?: number;
};

export type DevicesResponse = {
  devices: Device[];
  detail: string;
};

export type MatterDeviceMutationRequest =
  | { device_id: string; endpoint_id: number; name: string }
  | { device_id: string; endpoint_id: number; on: boolean }
  | {
      device_id: string;
      endpoint_id: number;
      level: number;
      transition_time_ds: number;
    }
  | {
      device_id: string;
      endpoint_id: number;
      color_temperature_mireds: number;
      transition_time_ds: number;
    };

export type MatterDeviceMutationResponse = MutationResponse & {
  operation_id?: number | null;
};

export type MatterDeviceRemoveRequest = { device_id: string };
export type MatterDeviceForgetRequest = {
  device_id: string;
  confirmation: string;
};
export type MatterDeviceForgetResponse = MutationResponse;
export type MatterDeviceRemoveResponse = MutationResponse & {
  operation_id?: number;
};

export type Room = {
  id: string;
  name: string;
};

export type Group = {
  id: string;
  name: string;
  device_ids: string[];
};

export type Scene = {
  id: string;
  name: string;
  actions: string[];
};

export type Automation = {
  id: string;
  name: string;
  enabled: boolean;
  trigger: string;
  actions: string[];
};

export type ModelDeleteRequest = {
  id: string;
};

export type RoomUpsertRequest = {
  id?: string | null;
  name: string;
};

export type GroupUpsertRequest = {
  id?: string | null;
  name: string;
  device_ids: string[];
};

export type SceneUpsertRequest = {
  id?: string | null;
  name: string;
  actions: string[];
};

export type AutomationUpsertRequest = {
  id?: string | null;
  name: string;
  enabled: boolean;
  trigger: string;
  actions: string[];
};

export type RoomsResponse = {
  rooms: Room[];
  detail: string;
};

export type RoomMutationResponse = MutationResponse & {
  room?: Room | null;
};

export type GroupsResponse = {
  groups: Group[];
  detail: string;
};

export type GroupMutationResponse = MutationResponse & {
  group?: Group | null;
};

export type ScenesResponse = {
  scenes: Scene[];
  detail: string;
};

export type SceneMutationResponse = MutationResponse & {
  scene?: Scene | null;
};

export type AutomationsResponse = {
  automations: Automation[];
  detail: string;
};

export type AutomationMutationResponse = MutationResponse & {
  automation?: Automation | null;
};

export const fallbackHealth: Health = {
  product: "Core Pilot",
  hostname: "corepilot.local",
  storage_ok: false,
  ai_enabled: false,
  auth: "none-trusted-lan",
};

export const fallbackNetwork: NetworkStatus = {
  mode: "setup mode",
  setup_ssid: "CorePilot-Setup-XXXX",
  station_ssid: null,
  station_bssid: null,
  ipv4: "192.168.4.1",
  station_ipv4: null,
  hostname: "corepilot.local",
  dhcp: "starting",
  http: "starting",
  wan_enabled: false,
  cloud_relay_enabled: false,
};

export const fallbackWifiScan: WifiScan = {
  networks: [],
  scanning: false,
  scan_state: "idle",
  detail: "Nearby Wi‑Fi networks will appear after Core Pilot scans.",
};

export const fallbackTime: TimeStatus = {
  ntp_server_id: "cloudflare",
  ntp_server_host: "time.cloudflare.com",
  timezone_id: "UTC",
  timezone_label: "UTC (UTC+00:00)",
  timezone_offset_minutes: 0,
  timezone_offset_label: "UTC+00:00",
  dns: "pending",
  ntp: "pending",
  rtc: "pending",
  last_sync_unix_seconds: null,
  current_unix_seconds: null,
  current_date: null,
  current_time: null,
  detail: "Time setup is loading.",
};

export const fallbackStorage: StorageStatus = {
  state: "setup needed",
  sd_root: "/core-pilot/",
  encrypted: false,
  mutations_allowed: false,
  device_seed_exportable: false,
  detail: "Storage setup is in progress.",
};

export const fallbackMatter: MatterStatus = {
  implementation: "Matter controller",
  commissioned: false,
  fabric_configured: false,
  subscriptions_active: 0,
  ipv6_ready: false,
  mdns_ready: false,
  dns_sd_ready: false,
  thread_ready: false,
  thread_dataset_configured: false,
  wifi_commissioning_ready: false,
  thread_commissioning_ready: false,
  adapter_status: "commissioning_adapter_missing",
  parser_ready: false,
  transport_adapter_ready: false,
  fabric_credentials_ready: false,
  pase_ready: false,
  case_ready: false,
  subscriptions_ready: false,
  operation: { active: null, pending: null, last_terminal: null },
  detail: "Matter setup is in progress.",
};

export const fallbackSmtp: SmtpStatus = {
  enabled: false,
  configured: false,
  host: "",
  port: 587,
  security: "start_tls",
  username_configured: false,
  password_configured: false,
  recipients: 0,
  transport_ready: false,
  detail: "SMTP notifications are not configured.",
};

export const fallbackAi: AiStatus = {
  enabled: false,
  provider_configured: false,
  mcp_enabled: true,
  max_tool_rounds: 8,
  max_repeated_tool_calls: 3,
  max_actions: 8,
  destructive_confirmation: "CONFIRM",
  persistence: "encrypted SD only",
  provider_transport: {
    http_ipv4_literal: true,
    dns: false,
    tls: false,
    websockets: false,
    certificate_validation: false,
    detail:
      "Firmware owns provider calls; local HTTP IPv4 providers are the current validated transport boundary.",
  },
  provider_transport_requirements: [],
};

export const fallbackSystem: SystemStatus = {
  product: "Core Pilot",
  hostname: "corepilot.local",
  firmware_version: "dev",
  setup_ap: "CorePilot-Setup-XXXX",
  display: "pending",
  storage: "uninitialized",
  matter: "setup needed",
  thread: "setup needed",
  ai: "disabled",
  ota: "local updates",
  ota_phase: "idle",
  ota_progress_bytes: 0,
  ota_total_bytes: 0,
  ota_restart_after_update: false,
  recovery: "on",
};

let apiQueue: Promise<void> = Promise.resolve();

const API_RETRY_DELAYS_MS = [150, 450];
const API_QUERY_TIMEOUT_MS = 6_000;
const API_MUTATION_TIMEOUT_MS = 15_000;
const AI_CHAT_RESULT_TIMEOUT_MS = 140_000;
const AI_CHAT_POLL_INTERVAL_MS = 500;

function abortReason(signal: globalThis.AbortSignal): unknown {
  return signal.reason ?? new DOMException("Request aborted", "AbortError");
}

function delay(ms: number, signal?: globalThis.AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) {
      reject(abortReason(signal));
      return;
    }
    const onAbort = () => {
      window.clearTimeout(timeout);
      signal?.removeEventListener("abort", onAbort);
      reject(abortReason(signal!));
    };
    const timeout = window.setTimeout(() => {
      signal?.removeEventListener("abort", onAbort);
      resolve();
    }, ms);
    signal?.addEventListener("abort", onAbort, { once: true });
  });
}

function enqueueApi<T>(operation: () => Promise<T>): Promise<T> {
  const queued = apiQueue.then(operation, operation);
  apiQueue = queued.then(
    () => undefined,
    () => undefined,
  );
  return queued;
}

async function fetchWithTimeout<T>(
  path: string,
  init: Parameters<typeof fetch>[1],
  timeoutMs: number,
  consume: (response: Response) => Promise<T>,
): Promise<T> {
  if (init?.signal?.aborted) throw abortReason(init.signal);

  const controller = new AbortController();
  const onCallerAbort = () => controller.abort(abortReason(init!.signal!));
  init?.signal?.addEventListener("abort", onCallerAbort, { once: true });
  const timeout = window.setTimeout(
    () =>
      controller.abort(
        new DOMException(`${path} request timed out`, "TimeoutError"),
      ),
    timeoutMs,
  );

  try {
    const headers = new Headers(init?.headers);
    for (const [key, value] of Object.entries(authHeaders()))
      headers.set(key, value);
    const response = await fetch(path, {
      ...init,
      headers: Object.fromEntries(headers.entries()),
      credentials: "omit",
      signal: controller.signal,
    });
    if (response.status === 401) clearAccess();
    return await consume(response);
  } finally {
    window.clearTimeout(timeout);
    init?.signal?.removeEventListener("abort", onCallerAbort);
  }
}

class ApiResponseError extends Error {
  constructor(
    readonly path: string,
    readonly status: number,
  ) {
    super(`${path} ${status}`);
    this.name = "ApiResponseError";
  }
}

function isTransientQueryError(error: unknown): boolean {
  if (error instanceof ApiResponseError)
    return [408, 425, 429, 500, 502, 503, 504].includes(error.status);
  if (error instanceof TypeError) return true;
  return (
    error instanceof DOMException &&
    (error.name === "TimeoutError" || error.name === "NetworkError")
  );
}

async function fetchWithTransientRetry<T>(
  path: string,
  init: Parameters<typeof fetch>[1],
  consume: (response: Response) => Promise<T>,
): Promise<T> {
  for (let attempt = 0; ; attempt += 1) {
    try {
      return await enqueueApi(() =>
        fetchWithTimeout(path, init, API_QUERY_TIMEOUT_MS, consume),
      );
    } catch (error) {
      if (init?.signal?.aborted) throw abortReason(init.signal);
      const delayMs = API_RETRY_DELAYS_MS[attempt];
      if (delayMs === undefined || !isTransientQueryError(error)) throw error;
      await delay(delayMs, init?.signal ?? undefined);
    }
  }
}

export function loadJson<T>(
  path: string,
  signal?: globalThis.AbortSignal,
): Promise<T> {
  return fetchWithTransientRetry(
    path,
    {
      headers: { accept: "application/json" },
      signal,
    },
    async (response) => {
      if (!response.ok) throw new ApiResponseError(path, response.status);
      return translateFirmwareError((await response.json()) as T);
    },
  );
}

const requestSchemas: Readonly<Record<string, z.ZodType>> = {
  "/api/v1/system/reboot": rebootRequestSchema,
  "/api/v1/network/wifi/scan": wifiScanRefreshRequestSchema,
  "/api/v1/network/wifi/scan/page": wifiScanPageRequestSchema,
  "/api/v1/activity/page": activityPageRequestSchema,
  "/api/v1/network/wifi": wifiProvisionRequestSchema,
  "/api/v1/network/wifi/forget": wifiForgetRequestSchema,
  "/api/v1/time": timeConfigRequestSchema,
  "/api/v1/smtp": smtpConfigRequestSchema,
  "/api/v1/smtp/test": smtpTestRequestSchema,
  "/api/v1/rooms/upsert": roomUpsertRequestSchema,
  "/api/v1/rooms/delete": modelDeleteRequestSchema,
  "/api/v1/groups/upsert": groupUpsertRequestSchema,
  "/api/v1/groups/delete": modelDeleteRequestSchema,
  "/api/v1/scenes/upsert": sceneUpsertRequestSchema,
  "/api/v1/scenes/delete": modelDeleteRequestSchema,
  "/api/v1/automations/upsert": automationUpsertRequestSchema,
  "/api/v1/automations/delete": modelDeleteRequestSchema,
  "/api/v1/automations/run": modelDeleteRequestSchema,
  "/api/v1/matter/commission": matterCommissionRequestSchema,
  "/api/v1/matter/commission/cancel": matterCommissionCancelRequestSchema,
  "/api/v1/devices": matterDeviceMutationSchema,
  "/api/v1/matter/devices/remove": matterDeviceRemoveRequestSchema,
  "/api/v1/matter/devices/forget": matterDeviceForgetRequestSchema,
  "/api/v1/matter/thread/dataset": threadDatasetUpdateRequestSchema,
  "/api/v1/ai/provider": aiProviderRequestSchema,
  "/api/v1/ai/chat": aiChatRequestSchema,
  "/api/v1/ai/memory/section": aiMemoryRequestSchema,
  "/api/v1/ai/prompts/file": aiPromptRequestSchema,
  "/api/v1/ai/mcp/tool": mcpToolPermissionRequestSchema,
  "/api/v1/ai/mcp/invoke": mcpInvokeRequestSchema,
  "/api/v1/ota/prepare": otaPrepareRequestSchema,
  "/api/v1/ota/chunk": otaChunkRequestSchema,
};

function mutationJson<TResponse, TRequest>(
  method: "PATCH" | "POST",
  path: string,
  body: TRequest,
  signal?: globalThis.AbortSignal,
): Promise<TResponse> {
  const schema = requestSchemas[path];
  if (!schema)
    return Promise.reject(new Error(`Missing request contract for ${path}`));
  const validation = schema.safeParse(body);
  if (!validation.success) return Promise.reject(validation.error);

  return enqueueApi(async () => {
    // Mutations are deliberately not retried. A response can be lost after the
    // device commits a write, and blindly replaying that POST can duplicate an
    // action or invalidate a sequential OTA upload.
    return fetchWithTimeout(
      path,
      {
        method,
        headers: {
          accept: "application/json",
          "content-type": "application/json",
        },
        body: JSON.stringify(validation.data),
        signal,
      },
      API_MUTATION_TIMEOUT_MS,
      async (response) => {
        if (!response.ok) throw new Error(`${path} ${response.status}`);
        return translateFirmwareError((await response.json()) as TResponse);
      },
    );
  });
}

export function postJson<TResponse, TRequest>(
  path: string,
  body: TRequest,
  signal?: globalThis.AbortSignal,
): Promise<TResponse> {
  return mutationJson<TResponse, TRequest>("POST", path, body, signal);
}

export function patchJson<TResponse, TRequest>(
  path: string,
  body: TRequest,
  signal?: globalThis.AbortSignal,
): Promise<TResponse> {
  return mutationJson<TResponse, TRequest>("PATCH", path, body, signal);
}

export function loadHealth(signal?: AbortSignal): Promise<Health> {
  return loadJson<Health>("/api/v1/health", signal);
}

export function loadSystem(signal?: AbortSignal): Promise<SystemStatus> {
  return loadJson<SystemStatus>("/api/v1/system", signal);
}

export function rebootSystem(): Promise<MutationResponse> {
  return postJson<MutationResponse, Record<string, never>>(
    "/api/v1/system/reboot",
    {},
  );
}

export function loadNetwork(signal?: AbortSignal): Promise<NetworkStatus> {
  return loadJson<NetworkStatus>("/api/v1/network", signal);
}

export function loadSavedWifiNetworks(
  signal?: AbortSignal,
): Promise<SavedWifiNetworks> {
  return loadJson<SavedWifiNetworks>("/api/v1/network/wifi/saved", signal);
}

export function loadSmtp(signal?: AbortSignal): Promise<SmtpStatus> {
  return loadJson<SmtpStatus>("/api/v1/smtp", signal);
}

export function saveSmtpConfig(
  request: SmtpConfigRequest,
): Promise<MutationResponse> {
  return postJson<MutationResponse, SmtpConfigRequest>("/api/v1/smtp", request);
}

export function sendSmtpTest(
  request: SmtpTestRequest,
): Promise<MutationResponse> {
  return postJson<MutationResponse, SmtpTestRequest>(
    "/api/v1/smtp/test",
    request,
  );
}

export function loadWifiScan(
  signal?: globalThis.AbortSignal,
): Promise<WifiScan> {
  return loadJson<WifiScan>("/api/v1/network/wifi/scan", signal);
}

export function refreshWifiScan(
  signal?: globalThis.AbortSignal,
): Promise<WifiScan> {
  return postJson<WifiScan, Record<string, never>>(
    "/api/v1/network/wifi/scan",
    {},
    signal,
  );
}

export function loadWifiScanPage(
  request: WifiScanPageRequest,
  signal?: globalThis.AbortSignal,
): Promise<WifiScanPage> {
  return postJson<WifiScanPage, WifiScanPageRequest>(
    "/api/v1/network/wifi/scan/page",
    request,
    signal,
  );
}

export async function loadActivityPage(
  request: ActivityPageRequest,
  signal?: globalThis.AbortSignal,
): Promise<ActivityPage> {
  const response = await postJson<ActivityPage, ActivityPageRequest>(
    "/api/v1/activity/page",
    request,
    signal,
  );
  return activityPageResponseSchema.parse(response);
}

const WIFI_SCAN_POLL_ATTEMPTS = 40;
const WIFI_SCAN_RECONNECT_DELAY_MS = 1_500;

function waitForScanPoll(signal: globalThis.AbortSignal): Promise<void> {
  return delay(750, signal);
}

export async function runWifiScanSession(
  signal: globalThis.AbortSignal,
  onPage?: (page: WifiScanPage) => void,
): Promise<WifiScan> {
  let scan = await refreshWifiScan(signal);
  for (let attempt = 0; attempt < WIFI_SCAN_POLL_ATTEMPTS; attempt += 1) {
    try {
      let lastSeen: string | null = null;
      const networks: WifiNetwork[] = [];
      let latestPage: WifiScanPage | null = null;
      for (let pageCount = 0; pageCount < 16; pageCount += 1) {
        const page = await loadWifiScanPage({ last_seen: lastSeen }, signal);
        latestPage = page;
        networks.push(...page.networks);
        onPage?.({ ...page, networks: [...networks] });
        lastSeen = page.next_last_seen ?? null;
        if (!lastSeen) break;
      }
      const pageState = latestPage?.scan_state ?? scan.scan_state;
      if (!["queued", "scanning"].includes(pageState)) {
        return {
          networks,
          scanning: latestPage?.scanning ?? scan.scanning,
          scan_state: pageState,
          detail: latestPage?.detail ?? scan.detail,
        };
      }
      await waitForScanPoll(signal);
      scan = await loadWifiScan(signal);
    } catch (error) {
      if (signal.aborted) throw abortReason(signal);
      if (attempt + 1 >= WIFI_SCAN_POLL_ATTEMPTS) throw error;
      await delay(WIFI_SCAN_RECONNECT_DELAY_MS, signal);
    }
  }
  return scan;
}

export function loadStorage(signal?: AbortSignal): Promise<StorageStatus> {
  return loadJson<StorageStatus>("/api/v1/storage", signal);
}

export function loadTime(signal?: AbortSignal): Promise<TimeStatus> {
  return loadJson<TimeStatus>("/api/v1/time", signal);
}

export function saveTimeConfig(
  request: TimeConfigRequest,
): Promise<MutationResponse> {
  return postJson<MutationResponse, TimeConfigRequest>("/api/v1/time", request);
}

export function loadDevices(signal?: AbortSignal): Promise<DevicesResponse> {
  return loadJson<DevicesResponse>("/api/v1/devices", signal);
}

export function updateMatterDevice(
  request: MatterDeviceMutationRequest,
): Promise<MatterDeviceMutationResponse> {
  return patchJson<MatterDeviceMutationResponse, MatterDeviceMutationRequest>(
    "/api/v1/devices",
    request,
  );
}

export function removeMatterDevice(
  request: MatterDeviceRemoveRequest,
): Promise<MatterDeviceRemoveResponse> {
  return postJson<MatterDeviceRemoveResponse, MatterDeviceRemoveRequest>(
    "/api/v1/matter/devices/remove",
    request,
  );
}

export function forgetMatterDevice(
  request: MatterDeviceForgetRequest,
): Promise<MatterDeviceForgetResponse> {
  return postJson<MatterDeviceForgetResponse, MatterDeviceForgetRequest>(
    "/api/v1/matter/devices/forget",
    request,
  );
}

export function loadRooms(signal?: AbortSignal): Promise<RoomsResponse> {
  return loadJson<RoomsResponse>("/api/v1/rooms", signal);
}

export function upsertRoom(
  request: RoomUpsertRequest,
): Promise<RoomMutationResponse> {
  return postJson<RoomMutationResponse, RoomUpsertRequest>(
    "/api/v1/rooms/upsert",
    request,
  );
}

export function deleteRoom(
  request: ModelDeleteRequest,
): Promise<RoomMutationResponse> {
  return postJson<RoomMutationResponse, ModelDeleteRequest>(
    "/api/v1/rooms/delete",
    request,
  );
}

export function loadGroups(signal?: AbortSignal): Promise<GroupsResponse> {
  return loadJson<GroupsResponse>("/api/v1/groups", signal);
}

export function upsertGroup(
  request: GroupUpsertRequest,
): Promise<GroupMutationResponse> {
  return postJson<GroupMutationResponse, GroupUpsertRequest>(
    "/api/v1/groups/upsert",
    request,
  );
}

export function deleteGroup(
  request: ModelDeleteRequest,
): Promise<GroupMutationResponse> {
  return postJson<GroupMutationResponse, ModelDeleteRequest>(
    "/api/v1/groups/delete",
    request,
  );
}

export function loadScenes(signal?: AbortSignal): Promise<ScenesResponse> {
  return loadJson<ScenesResponse>("/api/v1/scenes", signal);
}

export function upsertScene(
  request: SceneUpsertRequest,
): Promise<SceneMutationResponse> {
  return postJson<SceneMutationResponse, SceneUpsertRequest>(
    "/api/v1/scenes/upsert",
    request,
  );
}

export function deleteScene(
  request: ModelDeleteRequest,
): Promise<SceneMutationResponse> {
  return postJson<SceneMutationResponse, ModelDeleteRequest>(
    "/api/v1/scenes/delete",
    request,
  );
}

export function loadAutomations(
  signal?: AbortSignal,
): Promise<AutomationsResponse> {
  return loadJson<AutomationsResponse>("/api/v1/automations", signal);
}

export function upsertAutomation(
  request: AutomationUpsertRequest,
): Promise<AutomationMutationResponse> {
  return postJson<AutomationMutationResponse, AutomationUpsertRequest>(
    "/api/v1/automations/upsert",
    request,
  );
}

export function deleteAutomation(
  request: ModelDeleteRequest,
): Promise<AutomationMutationResponse> {
  return postJson<AutomationMutationResponse, ModelDeleteRequest>(
    "/api/v1/automations/delete",
    request,
  );
}

export function runAutomation(
  request: ModelDeleteRequest,
): Promise<AutomationMutationResponse> {
  return postJson<AutomationMutationResponse, ModelDeleteRequest>(
    "/api/v1/automations/run",
    request,
  );
}

export function loadMatter(signal?: AbortSignal): Promise<MatterStatus> {
  return loadJson<MatterStatus>("/api/v1/matter", signal);
}

export function commissionMatter(
  request: MatterCommissionRequest,
): Promise<MatterCommissionResponse> {
  return postJson<MatterCommissionResponse, MatterCommissionRequest>(
    "/api/v1/matter/commission",
    request,
  );
}

export function cancelMatterCommission(
  operationId: number,
): Promise<MatterCommissionCancelResponse> {
  return postJson<MatterCommissionCancelResponse, { operation_id: number }>(
    "/api/v1/matter/commission/cancel",
    { operation_id: operationId },
  );
}

export function updateThreadDataset(
  request: ThreadDatasetUpdateRequest,
): Promise<ThreadDatasetUpdateResponse> {
  return postJson<ThreadDatasetUpdateResponse, ThreadDatasetUpdateRequest>(
    "/api/v1/matter/thread/dataset",
    request,
  );
}

export async function loadAi(signal?: AbortSignal): Promise<AiStatus> {
  const response = await loadJson<Partial<AiStatus>>("/api/v1/ai", signal);
  return {
    ...fallbackAi,
    ...response,
    provider_transport: {
      ...fallbackAi.provider_transport,
      ...response.provider_transport,
    },
    provider_transport_requirements: Array.isArray(
      response.provider_transport_requirements,
    )
      ? response.provider_transport_requirements
      : fallbackAi.provider_transport_requirements,
  };
}

export function loadAiMemory(signal?: AbortSignal): Promise<AiMemoryResponse> {
  return loadJson<AiMemoryResponse>("/api/v1/ai/memory", signal);
}

export function loadAiPrompts(
  signal?: AbortSignal,
): Promise<AiPromptDirectoryResponse> {
  return loadJson<AiPromptDirectoryResponse>("/api/v1/ai/prompts", signal);
}

export async function sendAiChat(
  request: AiChatRequest,
): Promise<AiChatResponse> {
  const accepted = await postJson<AiChatResponse, AiChatRequest>(
    "/api/v1/ai/chat",
    request,
  );
  if (!accepted.accepted || accepted.status !== "processing") return accepted;
  if (!/^[1-9][0-9]*$/.test(accepted.operation_id))
    throw new Error("Core Pilot returned an invalid AI operation identifier.");

  const deadline = Date.now() + AI_CHAT_RESULT_TIMEOUT_MS;
  const statusPath = `/api/v1/ai/chat?operation_id=${encodeURIComponent(accepted.operation_id)}`;
  while (Date.now() < deadline) {
    await delay(AI_CHAT_POLL_INTERVAL_MS);
    const result = await loadJson<AiChatResponse>(statusPath);
    if (result.status !== "processing") return result;
  }
  throw new Error(
    "The AI provider did not finish before the response deadline.",
  );
}

export function saveAiMemorySection(
  request: AiMemoryUpdateRequest,
): Promise<MutationResponse> {
  return postJson<MutationResponse, AiMemoryUpdateRequest>(
    "/api/v1/ai/memory/section",
    request,
  );
}

function encodeUtf8ToBase64(value: string): string {
  const bytes = new globalThis.TextEncoder().encode(value);
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return globalThis.btoa(binary);
}

export function saveAiPromptFile(
  request: AiPromptUpdateRequest,
): Promise<MutationResponse> {
  return postJson<MutationResponse, AiPromptUpdateRequest>(
    "/api/v1/ai/prompts/file",
    {
      path: request.path,
      markdown: "",
      markdown_base64: encodeUtf8ToBase64(request.markdown),
    },
  );
}

export function provisionWifi(
  request: WifiProvisionRequest,
): Promise<MutationResponse> {
  return postJson<MutationResponse, WifiProvisionRequest>(
    "/api/v1/network/wifi",
    request,
  );
}

export function forgetWifiNetwork(
  request: WifiForgetRequest,
): Promise<MutationResponse> {
  return postJson<MutationResponse, WifiForgetRequest>(
    "/api/v1/network/wifi/forget",
    request,
  );
}

export function setupAiProvider(
  request: AiProviderSetupRequest,
): Promise<MutationResponse> {
  return postJson<MutationResponse, AiProviderSetupRequest>(
    "/api/v1/ai/provider",
    request,
  );
}

const mcpToolIds: Record<McpToolName, string> = {
  ListDevices: "devices.list",
  GetDeviceState: "devices.get_state",
  SetOnOff: "devices.set_power",
  SetLevel: "devices.set_level",
  SetColor: "devices.set_color",
  SetColorTemperature: "devices.set_color_temperature",
  ListRooms: "rooms.list",
  CreateRoom: "rooms.create",
  UpdateRoom: "rooms.update",
  DeleteRoom: "rooms.delete",
  ListGroups: "groups.list",
  CreateGroup: "groups.create",
  UpdateGroup: "groups.update",
  DeleteGroup: "groups.delete",
  ActivateScene: "scenes.activate",
  ListScenes: "scenes.list",
  CreateScene: "scenes.create",
  UpdateScene: "scenes.update",
  DeleteScene: "scenes.delete",
  ListAutomations: "automations.list",
  CreateAutomation: "automations.create",
  UpdateAutomation: "automations.update",
  DeleteAutomation: "automations.delete",
  RunAutomation: "automations.run",
  EnableAutomation: "automations.enable",
  DisableAutomation: "automations.disable",
  GetHubStatus: "hub.status",
  GetMatterOperation: "matter.get_operation",
  RemoveMatterDevice: "matter.remove_device",
};

const mcpToolNames = Object.keys(mcpToolIds) as McpToolName[];

function isMcpToolName(value: unknown): value is McpToolName {
  return (
    typeof value === "string" && mcpToolNames.includes(value as McpToolName)
  );
}

function titleFromIdentifier(identifier: string): string {
  return identifier
    .replaceAll(".", " ")
    .replaceAll("_", " ")
    .replace(/([a-z])([A-Z])/g, "$1 $2")
    .replace(/^\w/, (letter) => letter.toUpperCase());
}

function normalizeMcpTool(
  value: Partial<McpToolDescriptor> & {
    tool?: unknown;
    label?: unknown;
  },
  index: number,
): McpToolDescriptor {
  const rawName = value.name ?? value.tool;
  const canonicalName = isMcpToolName(rawName)
    ? rawName
    : (mcpToolNames.find((candidate) => mcpToolIds[candidate] === rawName) ??
      mcpToolNames[index] ??
      "GetHubStatus");
  const id = value.id?.trim() || mcpToolIds[canonicalName];
  const name =
    typeof rawName === "string" && rawName.trim() ? rawName : canonicalName;
  const title =
    value.title?.trim() ||
    (typeof value.label === "string" ? value.label.trim() : "") ||
    titleFromIdentifier(id);

  return {
    id,
    name,
    title,
    category: value.category?.trim() || "Capabilities",
    description: value.description?.trim() || "Local assistant capability.",
    enabled: value.enabled === true,
    destructive: value.destructive === true,
    available: value.available === true,
    ...(value.inputSchema && typeof value.inputSchema === "object"
      ? { inputSchema: value.inputSchema }
      : {}),
  };
}

function normalizeMcpTools(
  response: Partial<McpToolsResponse>,
): McpToolsResponse {
  const tools = Array.isArray(response.tools) ? response.tools : [];
  return {
    tools: tools.map((tool, index) => normalizeMcpTool(tool, index)),
  };
}

export async function loadMcpTools(
  signal?: AbortSignal,
): Promise<McpToolsResponse> {
  return normalizeMcpTools(
    await loadJson<McpToolsResponse>("/api/v1/ai/mcp/tools", signal),
  );
}

export function updateMcpToolPermission(
  request: McpToolPermissionUpdateRequest,
): Promise<MutationResponse> {
  return postJson<MutationResponse, McpToolPermissionUpdateRequest>(
    "/api/v1/ai/mcp/tool",
    request,
  );
}

export async function loadMcpContext(
  signal?: AbortSignal,
): Promise<McpContextResponse> {
  const response = await loadJson<McpContextResponse>(
    "/api/v1/ai/mcp/context",
    signal,
  );
  return {
    ...response,
    tools: normalizeMcpTools(response).tools,
  };
}

export function invokeMcpTool(
  request: McpInvokeRequest,
): Promise<McpInvokeResponse> {
  return postJson<McpInvokeResponse, McpInvokeRequest>(
    "/api/v1/ai/mcp/invoke",
    request,
  );
}

export function prepareOta(
  request: OtaPrepareRequest,
): Promise<MutationResponse> {
  return postJson<MutationResponse, OtaPrepareRequest>(
    "/api/v1/ota/prepare",
    request,
  );
}

export function uploadOtaChunk(
  request: OtaChunkRequest,
): Promise<MutationResponse> {
  return postJson<MutationResponse, OtaChunkRequest>(
    "/api/v1/ota/chunk",
    request,
  );
}
