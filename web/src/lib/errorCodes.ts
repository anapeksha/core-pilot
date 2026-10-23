export const ERROR_MESSAGES = {
  STORAGE_UNAVAILABLE:
    "Encrypted storage is unavailable. Check the SD card and restart Core Pilot.",
  STORAGE_WRITE_FAILED:
    "The change could not be written to encrypted storage. Check the SD card and try again.",
  WIFI_CONFIG_INVALID: "The Wi-Fi settings are invalid.",
  NTP_SERVER_UNSUPPORTED: "Select one of the supported time servers.",
  TIMEZONE_UNSUPPORTED: "Select a supported timezone and UTC offset.",
  SMTP_CONFIG_REJECTED: "The SMTP settings were rejected.",
  SMTP_NOT_CONFIGURED: "Save SMTP settings before sending a test message.",
  SMTP_CONFIG_INVALID: "The saved SMTP settings are invalid.",
  SMTP_MESSAGE_INVALID: "The SMTP test message is invalid or too large.",
  SMTP_TRANSPORT_UNAVAILABLE:
    "SMTP sending is unavailable until its network transport is validated.",
  MODEL_MUTATION_REJECTED:
    "The requested change is invalid or conflicts with an existing item.",
  AUTOMATION_NOT_FOUND: "The automation no longer exists.",
  AUTOMATION_NOT_MANUAL:
    "Only enabled manual automations can be run explicitly.",
  AUTOMATION_QUEUE_FULL:
    "The automation queue is busy. Wait briefly and try again.",
  MATTER_QUEUE_FULL:
    "Another Matter commissioning operation is already queued.",
  MATTER_REQUEST_INVALID: "The Matter setup request is invalid.",
  MATTER_NOT_READY:
    "Matter commissioning is not ready. Check fabric and network diagnostics.",
  MATTER_OPERATION_IDS_EXHAUSTED:
    "Matter operation identifiers are exhausted. Restart Core Pilot.",
  MATTER_OPERATION_NOT_FOUND:
    "The Matter commissioning operation was not found.",
  MATTER_OPERATION_TERMINAL:
    "The Matter commissioning operation has already finished.",
  MATTER_CANCELLATION_UNSAFE:
    "Only a Matter request still waiting in the pending slot can be cancelled.",
  MATTER_COMMISSIONING_BOOT_BUSY:
    "A Thread commissioning boot is already pending. Wait for it to finish before trying again.",
  MATTER_COMMISSIONING_PERSIST_FAILED:
    "The Thread commissioning request could not be saved securely. Core Pilot was not restarted.",
  MATTER_COMMISSIONING_TIME_UNAVAILABLE:
    "Thread commissioning needs trusted current time. Synchronize the Core Pilot clock and try again.",
  MATTER_COMMISSIONING_RECOVERY_REQUIRED:
    "A previous Thread commissioning boot was interrupted. Review the accessory state before retrying.",
  MATTER_COMMAND_QUEUE_FULL:
    "The Matter command queue is busy. Wait briefly and try again.",
  MATTER_COMMAND_TARGET_INVALID:
    "The selected Matter device or endpoint is no longer available.",
  MATTER_COMMAND_TARGET_AMBIGUOUS:
    "Select a specific Matter endpoint for this device.",
  MATTER_COMMAND_UNSUPPORTED:
    "The selected Matter endpoint does not support On/Off control.",
  MATTER_COMMAND_NOT_READY:
    "Matter control is not ready. Check the device connection and commissioning status.",
  DISCOVERY_FAILED: "Matter commissionable-device discovery failed.",
  PASE_FAILED:
    "The secure Matter commissioning session could not be established.",
  ATTESTATION_FAILED: "Matter device attestation failed.",
  CREDENTIAL_INSTALLATION_FAILED:
    "Matter operational credentials could not be installed.",
  NETWORK_CONFIGURATION_FAILED:
    "The device rejected its Matter network configuration.",
  NETWORK_CONNECT_FAILED:
    "The commissioned device could not connect to its network.",
  COMMISSIONING_COMPLETE_FAILED:
    "The device did not complete Matter commissioning.",
  OPERATIONAL_DISCOVERY_FAILED:
    "The commissioned Matter device could not be rediscovered.",
  CASE_FAILED:
    "The operational Matter secure session could not be established.",
  DESCRIPTOR_DISCOVERY_FAILED:
    "The Matter device endpoint capabilities could not be discovered.",
  SUBSCRIPTIONS_FAILED: "Matter subscriptions could not be established.",
  PERSISTENCE_FAILED:
    "The commissioned device state could not be saved securely.",
  PHASE_TIMED_OUT: "The Matter commissioning phase timed out.",
  RETRY_LIMIT_REACHED: "The Matter commissioning retry limit was reached.",
  WORKER_FAILED: "The Matter commissioning worker stopped unexpectedly.",
  THREAD_DATASET_INVALID:
    "Enter a valid hexadecimal Thread Active Operational Dataset.",
  AI_REQUEST_INVALID:
    "The AI request is empty, too large, or has too much history.",
  AI_BUSY: "Another AI request is already running.",
  AI_PROVIDER_UNAVAILABLE:
    "The configured AI provider did not return a usable response.",
  AI_PROMPT_INVALID: "The prompt file is invalid, empty, or too large.",
  AI_MEMORY_TOO_LARGE: "The AI memory section is too large.",
  AI_PROVIDER_CONFIG_INVALID: "The AI provider configuration is invalid.",
  AI_TOOL_UNAVAILABLE: "This AI tool is not available in the current firmware.",
  AI_TOOL_REQUEST_INVALID: "The AI tool request is invalid or too large.",
  AI_TOOL_REJECTED: "The AI tool request was rejected.",
  OTA_METADATA_INVALID: "The firmware image metadata is invalid.",
  OTA_CHUNK_REJECTED: "The firmware image chunk was rejected.",
} as const;

export type FirmwareErrorCode = keyof typeof ERROR_MESSAGES;

export function firmwareErrorMessage(code: string): string | undefined {
  return ERROR_MESSAGES[code as FirmwareErrorCode];
}

function translateMatterFailure(value: unknown): unknown {
  if (!value || typeof value !== "object" || Array.isArray(value)) return value;

  const operation = value as Record<string, unknown>;
  const failure = operation.failure;
  if (!failure || typeof failure !== "object" || Array.isArray(failure))
    return value;

  const nested = failure as Record<string, unknown>;
  const message =
    typeof nested.code === "string"
      ? firmwareErrorMessage(nested.code)
      : undefined;
  if (!message || typeof nested.detail !== "string") return value;

  return { ...operation, failure: { ...nested, detail: message } };
}

/** Adds presentation text to coded firmware failures at the client boundary. */
export function translateFirmwareError<T>(value: T): T {
  if (!value || typeof value !== "object" || Array.isArray(value)) return value;

  const response = value as Record<string, unknown>;
  let translated = response;

  if (typeof response.detail === "string") {
    const message = firmwareErrorMessage(response.detail);
    if (message) translated = { ...translated, detail: message };
  }

  if (response.accepted === false && typeof response.status === "string") {
    const message = firmwareErrorMessage(response.status);
    if (message) {
      translated = {
        ...translated,
        ...(typeof response.detail === "string" ? { detail: message } : {}),
        ...(typeof response.reply === "string" ? { reply: message } : {}),
      };
    }
  }

  translated = translateMatterFailure(translated) as Record<string, unknown>;

  const operation = translated.operation;
  if (operation && typeof operation === "object" && !Array.isArray(operation)) {
    const snapshot = operation as Record<string, unknown>;
    translated = {
      ...translated,
      operation: {
        ...snapshot,
        active: translateMatterFailure(snapshot.active),
        pending: translateMatterFailure(snapshot.pending),
        last_terminal: translateMatterFailure(snapshot.last_terminal),
      },
    };
  }

  return translated as T;
}
