import { describe, expect, it } from "vitest";
import { dashboardAuthPolicy, pageById, pageByPath, pages } from "./pages";
import { routePaths } from "./routePaths";

describe("dashboard metadata", () => {
  // Positive/negative: confirms the static LAN dashboard opens directly and that
  // no login/auth page has been introduced by route metadata.
  it("loads directly without an auth/login page", () => {
    expect(dashboardAuthPolicy).toBe("none-trusted-lan");
    expect(pages.map((page) => page.label)).toContain("Home");
    expect(pages.map((page) => page.label)).not.toContain("Login");
    expect(routePaths).not.toContain("/login" as never);
  });

  // Positive: resolves known page IDs and paths to the stable dashboard metadata
  // used by the shell and sidebar.
  it("keeps stable modular page lookup", () => {
    expect(pageById("ai").label).toBe("Assistant");
    expect(pageByPath("/ai/tools").label).toBe("Assistant");
    expect(pageByPath("/ai/prompts").label).toBe("Assistant");
    expect(pageByPath("/system").label).toBe("System");
  });

  // Negative: unknown IDs and paths fall back to overview instead of creating a
  // broken route or blank dashboard shell.
  it("falls back to the overview page for unknown internal routes", () => {
    expect(pageByPath("/missing").id).toBe("overview");
    expect(pageByPath("/devices/add").id).toBe("devices");
    expect(pageById("missing" as never).id).toBe("overview");
  });

  // Positive: every sidebar page is backed by a declared SPA route path so
  // navigation metadata and router configuration cannot drift silently.
  it("declares every page as a SPA route path", () => {
    for (const page of pages) {
      expect(routePaths).toContain(page.path as (typeof routePaths)[number]);
    }
  });

  // Negative/architecture: Matter setup remains under Devices and does not become
  // a separate top-level navigation bucket.
  it("keeps Matter actions nested under the Devices domain", () => {
    expect(pages.map((page) => page.label)).not.toContain("Add Matter Device");
    expect(pages.map((page) => page.label)).not.toContain("Matter Setup");
    expect(routePaths).toContain("/devices/add");
    expect(routePaths).toContain("/devices/configure");
  });
});
