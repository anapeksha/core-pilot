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
        new Response(JSON.stringify({ authentication_required: true })),
      )
      .mockResolvedValueOnce(
        new Response(
          JSON.stringify({ token: "a".repeat(64), expires_in: 28800 }),
        ),
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
  await unlockAccess("0123456789abcdef");
  expect(root.textContent).toContain("Protected dashboard");
  clearAccess();
  expect(root.textContent).not.toContain("Protected dashboard");
});
