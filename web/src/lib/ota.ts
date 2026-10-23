import { MAX_OTA_IMAGE_BYTES } from "./api";

export const OTA_CHUNK_BYTES = 2 * 1024;

export type OtaRecoveryStatus = {
  ota_phase: string;
  ota_progress_bytes: number;
  ota_total_bytes: number;
};

export function otaChunkCanRetry(
  status: OtaRecoveryStatus,
  expectedTotalBytes: number,
  chunkOffset: number,
) {
  return (
    status.ota_total_bytes === expectedTotalBytes &&
    status.ota_progress_bytes === chunkOffset &&
    ["prepared", "staging"].includes(status.ota_phase)
  );
}

export function otaChunkWasCommitted(
  status: OtaRecoveryStatus,
  expectedTotalBytes: number,
  expectedEndOffset: number,
  finalChunk: boolean,
) {
  if (
    status.ota_total_bytes !== expectedTotalBytes ||
    status.ota_progress_bytes !== expectedEndOffset
  ) {
    return false;
  }

  return finalChunk
    ? ["activated", "rebooting"].includes(status.ota_phase)
    : status.ota_phase === "staging";
}

const FULL_FLASH_IMAGE_PATTERN =
  /(^|[-_])(merged|bootloader|partition-table|partitions)([-_.]|$)/i;

export type FirmwareCandidate = {
  name: string;
  size: number;
};

export function formatOtaSize(size: number) {
  if (!size) return "Select a .bin file";
  if (size < 1024 * 1024) return `${Math.round(size / 1024)} KB`;
  return `${(size / (1024 * 1024)).toFixed(2)} MB`;
}

export function firmwareVersionFromFilename(name: string) {
  return name.replace(/\.bin$/i, "").trim();
}

export function validateFirmwareCandidate(file: FirmwareCandidate | null) {
  if (!file) return "";
  if (!file.name.toLowerCase().endsWith(".bin")) {
    return "Select the app firmware .bin built for the OTA app slot.";
  }
  if (FULL_FLASH_IMAGE_PATTERN.test(file.name)) {
    return "Select the app firmware .bin for OTA, not merged-binary.bin, bootloader.bin, partition-table.bin, or a full flash image.";
  }
  if (file.size <= 0) {
    return "Selected firmware image is empty.";
  }
  if (file.size > MAX_OTA_IMAGE_BYTES) {
    return `OTA accepts app images up to ${formatOtaSize(MAX_OTA_IMAGE_BYTES)}; selected file is ${formatOtaSize(file.size)}.`;
  }
  return "";
}
