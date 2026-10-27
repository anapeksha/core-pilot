import { describe, expect, it } from "vitest";
import { fallbackTime } from "../lib/api";
import { timeRefetchInterval } from "./time";

describe("time query", () => {
  it("refetches at a bounded interval only while NTP is synchronizing", () => {
    expect(timeRefetchInterval({ ...fallbackTime, ntp: "syncing" })).toBe(
      2_000,
    );
    expect(timeRefetchInterval({ ...fallbackTime, ntp: "synchronized" })).toBe(
      false,
    );
    expect(timeRefetchInterval(undefined)).toBe(false);
  });
});
