import { describe, expect, it } from "vitest";
import {
  firmwareVersionFromFilename,
  formatOtaSize,
  OTA_CHUNK_BYTES,
  otaChunkCanRetry,
  otaChunkWasCommitted,
  validateFirmwareCandidate,
} from "./ota";
import { MAX_OTA_IMAGE_BYTES } from "./api";

describe("ota helpers", () => {
  // Positive: app firmware images within the inactive-slot bound can proceed to
  // browser SHA-256 and bounded direct inactive-slot upload.
  it("accepts bounded app firmware images", () => {
    expect(
      validateFirmwareCandidate({
        name: "core_pilot_firmware.bin",
        size: MAX_OTA_IMAGE_BYTES,
      }),
    ).toBe("");
    expect(firmwareVersionFromFilename("core_pilot_firmware.bin")).toBe(
      "core_pilot_firmware",
    );
    expect(OTA_CHUNK_BYTES).toBe(2048);
  });

  // Negative: full flash artifacts are rejected client-side before the firmware
  // receives metadata for an image it must not write to the OTA app slot.
  it("rejects merged full-flash and partition artifacts", () => {
    expect(
      validateFirmwareCandidate({ name: "merged-binary.bin", size: 1_000_000 }),
    ).toContain("not merged-binary.bin");
    expect(
      validateFirmwareCandidate({ name: "partition-table.bin", size: 3_072 }),
    ).toContain("partition-table.bin");
    expect(
      validateFirmwareCandidate({ name: "bootloader.bin", size: 30_000 }),
    ).toContain("bootloader.bin");
  });

  // Negative: size and extension checks mirror the firmware's bounded OTA
  // metadata validation so the Validate & upload button is enabled only for a
  // candidate the firmware can accept.
  it("only retries when fresh device state proves the chunk was not written", () => {
    const prepared = {
      ota_phase: "prepared",
      ota_progress_bytes: 0,
      ota_total_bytes: 8192,
    };
    expect(otaChunkCanRetry(prepared, 8192, 0)).toBe(true);
    expect(otaChunkCanRetry(prepared, 4096, 0)).toBe(false);
    expect(otaChunkCanRetry(prepared, 8192, 2048)).toBe(false);
    expect(
      otaChunkCanRetry({ ...prepared, ota_phase: "failed" }, 8192, 0),
    ).toBe(false);
  });

  it("only recovers a lost response from the exact matching device state", () => {
    const staging = {
      ota_phase: "staging",
      ota_progress_bytes: 2048,
      ota_total_bytes: 8192,
    };
    expect(otaChunkWasCommitted(staging, 8192, 2048, false)).toBe(true);
    expect(otaChunkWasCommitted(staging, 8192, 1024, false)).toBe(false);
    expect(otaChunkWasCommitted(staging, 4096, 2048, false)).toBe(false);
    expect(
      otaChunkWasCommitted(
        { ...staging, ota_phase: "failed" },
        8192,
        2048,
        false,
      ),
    ).toBe(false);
    expect(
      otaChunkWasCommitted(
        {
          ota_phase: "activated",
          ota_progress_bytes: 8192,
          ota_total_bytes: 8192,
        },
        8192,
        8192,
        true,
      ),
    ).toBe(true);
    expect(otaChunkWasCommitted(staging, 8192, 2048, true)).toBe(false);
  });

  it("rejects oversized empty and non-bin candidates", () => {
    expect(validateFirmwareCandidate({ name: "firmware.hex", size: 100 })).toBe(
      "Select the app firmware .bin built for the OTA app slot.",
    );
    expect(validateFirmwareCandidate({ name: "firmware.bin", size: 0 })).toBe(
      "Selected firmware image is empty.",
    );
    expect(
      validateFirmwareCandidate({
        name: "firmware.bin",
        size: MAX_OTA_IMAGE_BYTES + 1,
      }),
    ).toContain(formatOtaSize(MAX_OTA_IMAGE_BYTES));
  });
});
