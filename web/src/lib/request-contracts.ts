import { z } from "zod";

const MAX_OTA_IMAGE_BYTES = 3 * 1024 * 1024;

export const entityLimits = {
  id: 48,
  name: 64,
  detail: 96,
  notification: 72,
  groupMembers: 32,
  actions: 8,
  hostname: 69,
  timezone: 64,
  smtpHost: 253,
  smtpUsername: 128,
  smtpPassword: 256,
  email: 254,
  smtpSubject: 160,
  smtpBody: 4096,
  recipients: 4,
  setupPayload: 21,
  threadDatasetHex: 508,
  providerUrl: 2048,
  providerModel: 128,
  apiKey: 256,
  chatMessage: 4096,
  chatHistory: 16,
  chatHistoryEntry: 4096,
  memoryId: 48,
  memoryMarkdown: 12_288,
  promptPath: 128,
  promptMarkdown: 4096,
  promptMarkdownBase64: 5464,
  mcpArguments: 8192,
} as const;

const utf8 = new TextEncoder();
export const bytesAtMost = (limit: number, message: string) =>
  z.string().refine((value) => utf8.encode(value).length <= limit, message);
const trimmedRequired = (limit: number, label: string) =>
  bytesAtMost(limit, `${label} is too long.`).refine(
    (value) => value.trim().length > 0,
    `${label} is required.`,
  );
const modelId = trimmedRequired(entityLimits.id, "ID").regex(
  /^[A-Za-z0-9_-]+$/,
  "ID contains unsupported characters.",
);
const modelName = trimmedRequired(entityLimits.name, "Name");
const detail = bytesAtMost(entityLimits.detail, "Value is too long.");
const optionalModelId = modelId.nullish();
const emptyRequest = z.object({}).strict();

export const rebootRequestSchema = emptyRequest;
export const wifiScanRefreshRequestSchema = emptyRequest;
export const wifiScanPageRequestSchema = z
  .object({ last_seen: z.string().nullable().optional() })
  .strict();

export const activityPageRequestSchema = z
  .object({
    before_sequence: z
      .number()
      .int()
      .min(0)
      .max(0xffffffff)
      .nullable()
      .optional(),
    limit: z.number().int().min(1).max(32).optional(),
  })
  .strict();

const hostname = bytesAtMost(
  entityLimits.hostname,
  "Hostname is too long.",
).refine((value) => {
  const label = value.toLowerCase().endsWith(".local")
    ? value.slice(0, -6)
    : value;
  return (
    label.length > 0 &&
    utf8.encode(label).length <= 63 &&
    /^[A-Za-z0-9-]+$/.test(label) &&
    !label.startsWith("-") &&
    !label.endsWith("-")
  );
}, "Hostname must be one DNS label, optionally followed by .local.");

export const wifiProvisionRequestSchema = z
  .object({
    credentials: z
      .object({
        ssid: bytesAtMost(32, "SSID is too long.").min(1, "SSID is required."),
        password: bytesAtMost(63, "Wi-Fi password is too long.").refine(
          (value) => value.length === 0 || utf8.encode(value).length >= 8,
          "Wi-Fi passwords must be empty or at least 8 bytes.",
        ),
      })
      .strict(),
    hostname: hostname.optional(),
  })
  .strict();

export const timeConfigRequestSchema = z
  .object({
    ntp_server_id: z.enum(["cloudflare", "google", "ntp_pool"]),
    timezone_id: bytesAtMost(
      entityLimits.timezone,
      "Timezone is too long.",
    ).refine((value) => {
      const trimmed = value.trim();
      return (
        trimmed.length > 0 &&
        /^[A-Za-z0-9/_+-]+$/.test(trimmed) &&
        !trimmed.startsWith("/") &&
        !trimmed.endsWith("/") &&
        !trimmed.includes("//")
      );
    }, "Timezone is invalid."),
    timezone_offset_minutes: z.number().int().min(-720).max(840),
  })
  .strict();

const email = bytesAtMost(
  entityLimits.email,
  "Email address is too long.",
).refine((value) => {
  const trimmed = value.trim();
  const [local, domain, ...rest] = trimmed.split("@");
  return (
    rest.length === 0 &&
    Boolean(local) &&
    Boolean(domain) &&
    !/\s/.test(trimmed) &&
    domain.includes(".") &&
    !domain.startsWith(".") &&
    !domain.endsWith(".")
  );
}, "Email address is invalid.");

export const smtpConfigRequestSchema = z
  .object({
    config: z
      .object({
        enabled: z.boolean(),
        host: bytesAtMost(entityLimits.smtpHost, "SMTP host is too long."),
        port: z.number().int().min(0).max(65_535),
        security: z.enum(["start_tls", "tls", "plain_lan_only"]),
        username: bytesAtMost(
          entityLimits.smtpUsername,
          "SMTP username is too long.",
        ),
        password: bytesAtMost(
          entityLimits.smtpPassword,
          "SMTP password is too long.",
        ).nullish(),
        from: bytesAtMost(entityLimits.email, "From address is too long."),
        recipients: z.array(email).max(entityLimits.recipients),
      })
      .strict(),
  })
  .strict()
  .superRefine(({ config }, context) => {
    if (!config.enabled) return;
    if (!config.host.trim())
      context.addIssue({
        code: "custom",
        path: ["config", "host"],
        message: "SMTP host is required.",
      });
    if (config.port === 0)
      context.addIssue({
        code: "custom",
        path: ["config", "port"],
        message: "SMTP port is required.",
      });
    if (!email.safeParse(config.from).success)
      context.addIssue({
        code: "custom",
        path: ["config", "from"],
        message: "From address is invalid.",
      });
    if (config.recipients.length === 0)
      context.addIssue({
        code: "custom",
        path: ["config", "recipients"],
        message: "At least one recipient is required.",
      });
  });

export const smtpTestRequestSchema = z
  .object({
    message: z
      .object({
        subject: trimmedRequired(entityLimits.smtpSubject, "Subject"),
        body: trimmedRequired(entityLimits.smtpBody, "Message"),
        recipients: z.array(email).max(entityLimits.recipients),
      })
      .strict(),
  })
  .strict();

export const modelDeleteRequestSchema = z.object({ id: modelId }).strict();
export const roomUpsertRequestSchema = z
  .object({ id: optionalModelId, name: modelName })
  .strict();
export const groupUpsertRequestSchema = z
  .object({
    id: optionalModelId,
    name: modelName,
    device_ids: z.array(detail).max(entityLimits.groupMembers),
  })
  .strict();
export const sceneUpsertRequestSchema = z
  .object({
    id: optionalModelId,
    name: modelName,
    actions: z.array(detail).max(entityLimits.actions),
  })
  .strict();
export const automationUpsertRequestSchema = z
  .object({
    id: optionalModelId,
    name: modelName,
    enabled: z.boolean(),
    trigger: trimmedRequired(entityLimits.detail, "Trigger"),
    actions: z.array(detail).min(1).max(entityLimits.actions),
  })
  .strict();

export const matterCommissionCancelRequestSchema = z
  .object({ operation_id: z.number().int().positive() })
  .strict();

export const matterOnOffRequestSchema = z
  .object({
    device_id: trimmedRequired(64, "Device ID"),
    endpoint_id: z.number().int().min(0).max(0xffff).optional(),
    on: z.boolean(),
  })
  .strict();

export const threadDatasetUpdateRequestSchema = z
  .object({
    active_dataset_tlvs_hex: z
      .string()
      .trim()
      .min(1, "Thread dataset TLVs are required.")
      .max(
        entityLimits.threadDatasetHex,
        `Thread dataset TLVs must be ${entityLimits.threadDatasetHex} characters or fewer.`,
      )
      .regex(
        /^[0-9a-fA-F]+$/,
        "Use hexadecimal characters only, without spaces or separators.",
      )
      .refine((value) => value.length % 2 === 0, {
        message: "Thread dataset TLVs must contain complete hexadecimal bytes.",
      })
      .transform((value) => value.toLowerCase()),
  })
  .strict();

export const matterCommissionRequestSchema = z
  .object({
    transport: z.enum(["wifi", "thread"]),
    setup_payload: trimmedRequired(entityLimits.setupPayload, "Setup code"),
    discriminator: z.number().int().min(0).max(4095).optional(),
    thread_dataset_id: modelId.optional(),
  })
  .strict();

const mcpToolName = z.enum([
  "ListDevices",
  "GetDeviceState",
  "SetOnOff",
  "SetLevel",
  "SetColor",
  "SetColorTemperature",
  "ActivateScene",
  "ListScenes",
  "ListAutomations",
  "EnableAutomation",
  "DisableAutomation",
  "CreateSimpleAutomation",
  "GetHubStatus",
  "RemoveMatterDevice",
  "ResetMatterFabric",
  "RestartHub",
  "ShutdownHub",
  "FactoryReset",
]);
const mcpToolId = z.enum([
  "devices.list",
  "devices.get_state",
  "devices.set_power",
  "devices.set_level",
  "devices.set_color",
  "devices.set_color_temperature",
  "scenes.activate",
  "scenes.list",
  "automations.list",
  "automations.enable",
  "automations.disable",
  "automations.create_simple",
  "hub.status",
  "matter.remove_device",
  "matter.reset_fabric",
  "hub.restart",
  "hub.shutdown",
  "hub.factory_reset",
]);
const mcpTool = z.union([mcpToolName, mcpToolId]);

export const aiProviderRequestSchema = z
  .object({
    provider: z
      .object({
        backend: z.enum(["open_ai_compatible", "anthropic_compatible"]),
        auth_type: z.enum(["bearer", "api_key", "none"]),
        base_url: trimmedRequired(entityLimits.providerUrl, "Base URL"),
        model: trimmedRequired(entityLimits.providerModel, "Model"),
        max_output_tokens: z.number().int().min(1).max(8192),
        timeout_ms: z.number().int().min(1000).max(120_000),
        temperature: z.number().finite().optional(),
        api_key: bytesAtMost(
          entityLimits.apiKey,
          "API key is too long.",
        ).nullish(),
        api_key_redacted: z.boolean(),
      })
      .strict(),
  })
  .strict();
export const aiChatRequestSchema = z
  .object({
    message: trimmedRequired(entityLimits.chatMessage, "Message"),
    history: z
      .array(
        z
          .object({
            role: z.enum(["user", "assistant", "tool"]),
            content: bytesAtMost(
              entityLimits.chatHistoryEntry,
              "History entry is too long.",
            ),
          })
          .strict(),
      )
      .max(entityLimits.chatHistory),
    chat_id: modelId.optional(),
    allow_actions: z.boolean(),
  })
  .strict();
export const aiMemoryRequestSchema = z
  .object({
    id: modelId,
    markdown: bytesAtMost(
      entityLimits.memoryMarkdown,
      "Memory section is too long.",
    ),
  })
  .strict();
export const aiPromptRequestSchema = z
  .object({
    path: trimmedRequired(entityLimits.promptPath, "Prompt path").regex(
      /^[A-Za-z0-9_./-]+$/,
      "Prompt path contains unsupported characters.",
    ),
    markdown: bytesAtMost(entityLimits.promptMarkdown, "Prompt is too long."),
    markdown_base64: bytesAtMost(
      entityLimits.promptMarkdownBase64,
      "Encoded prompt is too long.",
    ).nullish(),
  })
  .strict();
export const mcpToolPermissionRequestSchema = z
  .object({ tool: mcpTool, enabled: z.boolean() })
  .strict();
export const mcpInvokeRequestSchema = z
  .object({
    tool: mcpTool,
    arguments_json: bytesAtMost(
      entityLimits.mcpArguments,
      "Tool arguments are too long.",
    ).refine((value) => {
      try {
        JSON.parse(value);
        return true;
      } catch {
        return false;
      }
    }, "Tool arguments must be valid JSON."),
  })
  .strict();

const base64 = z
  .string()
  .regex(
    /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/,
    "Invalid Base64 data.",
  );
export const otaPrepareRequestSchema = z
  .object({
    version: z.string().trim().min(1),
    size_bytes: z.number().int().min(1).max(MAX_OTA_IMAGE_BYTES),
    sha256_hex: z
      .string()
      .regex(
        /^[0-9a-fA-F]{64}$/,
        "SHA-256 must contain 64 hexadecimal characters.",
      ),
    restart_after_update: z.boolean(),
  })
  .strict();
export const otaChunkRequestSchema = z
  .object({
    offset: z.number().int().min(0).max(0xffff_ffff),
    data_base64: base64.max(3072),
    final_chunk: z.boolean(),
  })
  .strict();
