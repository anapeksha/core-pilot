import { describe, expect, it } from "vitest";

import { requiresDestructiveToolConfirmation } from "./Tools";

describe("destructive MCP tool permission interactions", () => {
  it("requires confirmation only when enabling a destructive tool", () => {
    expect(
      requiresDestructiveToolConfirmation(
        { destructive: true, enabled: false },
        true,
      ),
    ).toBe(true);
    expect(
      requiresDestructiveToolConfirmation(
        { destructive: true, enabled: true },
        false,
      ),
    ).toBe(false);
    expect(
      requiresDestructiveToolConfirmation(
        { destructive: false, enabled: false },
        true,
      ),
    ).toBe(false);
  });
});
