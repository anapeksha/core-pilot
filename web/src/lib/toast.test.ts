import { describe, expect, it } from "vitest";

import { createToastGate } from "./toast";

describe("toast gate", () => {
  it("suppresses duplicate reactive updates", () => {
    const shouldShow = createToastGate();
    expect(shouldShow("error:request failed")).toBe(true);
    expect(shouldShow("error:request failed")).toBe(false);
  });

  it("allows the same error after the operation returns to idle", () => {
    const shouldShow = createToastGate();
    expect(shouldShow("error:request failed")).toBe(true);
    expect(shouldShow("")).toBe(false);
    expect(shouldShow("error:request failed")).toBe(true);
  });
});
