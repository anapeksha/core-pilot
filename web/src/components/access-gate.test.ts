import { afterEach, expect, it, vi } from "vitest";
import { createComponent } from "solid-js";
import { render } from "solid-js/web";
import AccessGate from "./access-gate";
import { clearAccess, unlockAccess } from "~/lib/auth";

let dispose: (() => void) | undefined;
afterEach(() => {
  dispose?.();
  clearAccess();
  document.body.replaceChildren();
  vi.unstubAllGlobals();
});
it("does not mount dashboard content before access and hides it on expiry", async () => {
  vi.stubGlobal(
    "fetch",
    vi
      .fn()
      .mockResolvedValueOnce(
        new Response(
          JSON.stringify({
            authentication_required: true,
            bootstrap_state: "open",
          }),
        ),
      )
      .mockResolvedValueOnce(
        new Response(JSON.stringify({ authenticated: true, expires_in: 1800 })),
      ),
  );
  const root = document.createElement("div");
  document.body.append(root);
  let mounted = false;
  dispose = render(
    () =>
      createComponent(AccessGate, {
        get children() {
          mounted = true;
          return "Protected dashboard";
        },
      }),
    root,
  );
  expect(mounted).toBe(false);
  await vi.waitFor(() => expect(root.textContent).toContain("Bootstrap code"));
  expect(root.textContent).toContain("expires after 30 minutes");
  expect(root.textContent).not.toContain("eight hours");
  await unlockAccess("0123456789abcdef");
  expect(root.textContent).toContain("Protected dashboard");
  clearAccess();
  expect(root.textContent).not.toContain("Protected dashboard");
});

it("waits for a physical code and explicitly rechecks without mutations", async () => {
  const fetcher = vi
    .fn()
    .mockResolvedValueOnce(
      new Response(
        JSON.stringify({
          authentication_required: true,
          bootstrap_state: "idle",
          session_authenticated: false,
        }),
      ),
    )
    .mockResolvedValueOnce(
      new Response(
        JSON.stringify({
          authentication_required: true,
          bootstrap_state: "open",
          session_authenticated: false,
        }),
      ),
    );
  vi.stubGlobal("fetch", fetcher);
  const root = document.createElement("div");
  document.body.append(root);
  dispose = render(
    () => createComponent(AccessGate, { children: "Protected dashboard" }),
    root,
  );
  await vi.waitFor(() => expect(root.textContent).toContain("I have a code"));
  expect(root.textContent).toContain("Authorize browser");
  expect(root.querySelector("input")).toBeNull();
  expect(root.textContent).not.toContain("Protected dashboard");
  expect(fetcher).toHaveBeenCalledTimes(1);
  root.querySelector<HTMLButtonElement>("button")!.click();
  await vi.waitFor(() => expect(root.querySelector("input")).not.toBeNull());
  expect(root.textContent).toContain("five minutes after that tap");
  expect(fetcher).toHaveBeenCalledTimes(2);
  for (const [url, options] of fetcher.mock.calls) {
    expect(url).toBe("/api/v1/auth");
    expect(options).not.toHaveProperty("method", "POST");
  }
});

it.each(["consumed", "expired", "attempts_exhausted", "unavailable"])(
  "offers physical recovery for %s",
  async (bootstrap_state) => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue(
        new Response(
          JSON.stringify({
            authentication_required: true,
            bootstrap_state,
          }),
        ),
      ),
    );
    const root = document.createElement("div");
    document.body.append(root);
    dispose = render(() => createComponent(AccessGate, {}), root);
    await vi.waitFor(() => expect(root.textContent).toContain("Check access"));
    expect(root.textContent).toContain("Authorize browser");
    expect(root.querySelector("input")).toBeNull();
  },
);

it("keeps transport errors safe and offers an explicit retry", async () => {
  vi.stubGlobal(
    "fetch",
    vi.fn().mockRejectedValue(new Error("private transport detail")),
  );
  const root = document.createElement("div");
  document.body.append(root);
  dispose = render(() => createComponent(AccessGate, {}), root);
  await vi.waitFor(() =>
    expect(root.querySelector('[role="alert"]')?.textContent).toBe(
      "Could not check device access.",
    ),
  );
  expect(root.textContent).not.toContain("private transport detail");
  expect(root.textContent).toContain("Check access");
  expect(root.querySelector("input")).toBeNull();
});
