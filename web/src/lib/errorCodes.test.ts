import { describe, expect, it } from "vitest";

import { translateFirmwareError } from "~/lib/errorCodes";

describe("firmware error translation", () => {
  it("translates a known mutation error at the API boundary", () => {
    expect(
      translateFirmwareError({
        accepted: false,
        status: "STORAGE_WRITE_FAILED",
        detail: "",
      }),
    ).toEqual({
      accepted: false,
      status: "STORAGE_WRITE_FAILED",
      detail:
        "The change could not be written to encrypted storage. Check the SD card and try again.",
    });
  });

  it("translates coded read failures carried by a detail field", () => {
    expect(
      translateFirmwareError({ rooms: [], detail: "STORAGE_UNAVAILABLE" }),
    ).toEqual({
      rooms: [],
      detail:
        "Encrypted storage is unavailable. Check the SD card and restart Core Pilot.",
    });
  });

  it("uses the reply field for coded AI failures", () => {
    expect(
      translateFirmwareError({
        accepted: false,
        status: "AI_PROVIDER_UNAVAILABLE",
        reply: "",
        actions: [],
      }),
    ).toMatchObject({
      status: "AI_PROVIDER_UNAVAILABLE",
      reply: "The configured AI provider did not return a usable response.",
    });
  });

  it("translates nested Matter operation failures", () => {
    expect(
      translateFirmwareError({
        id: 7,
        failure: { code: "PASE_FAILED", detail: "" },
      }),
    ).toEqual({
      id: 7,
      failure: {
        code: "PASE_FAILED",
        detail:
          "The secure Matter commissioning session could not be established.",
      },
    });
  });

  it("leaves successful and unknown responses unchanged", () => {
    const success = { accepted: true, status: "saved", detail: "Saved." };
    const unknown = { accepted: false, status: "FUTURE_CODE", detail: "" };

    expect(translateFirmwareError(success)).toBe(success);
    expect(translateFirmwareError(unknown)).toBe(unknown);
  });
});
