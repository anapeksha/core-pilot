import { createComponent, type JSX } from "solid-js";
import { render } from "solid-js/web";
import { HiOutlinePencil } from "solid-icons/hi";
import { afterEach, describe, expect, it, vi } from "vitest";

import "~/app.css";
import { Stepper } from "~/components/stepper";
import { TooltipIconButton } from "~/components/tooltip";

let dispose: (() => void) | undefined;

afterEach(() => {
  dispose?.();
  dispose = undefined;
  document.body.replaceChildren();
});

function renderIntoBody(component: () => JSX.Element) {
  const host = document.createElement("div");
  document.body.append(host);
  dispose = render(component, host);
  return host;
}

describe("icon-only button tooltips", () => {
  it("shows a tooltip on hover while preserving the accessible name", async () => {
    const host = renderIntoBody(() =>
      createComponent(TooltipIconButton, {
        "aria-label": "Edit kitchen",
        children: createComponent(HiOutlinePencil, { "aria-hidden": "true" }),
      }),
    );
    const button = host.querySelector<HTMLButtonElement>(
      'button[aria-label="Edit kitchen"]',
    );

    expect(button).not.toBeNull();
    expect(host.querySelectorAll("button button")).toHaveLength(0);
    button?.dispatchEvent(
      new PointerEvent("pointerenter", { bubbles: true, pointerType: "mouse" }),
    );
    button?.dispatchEvent(
      new PointerEvent("pointermove", { bubbles: true, pointerType: "mouse" }),
    );

    await vi.waitFor(() => {
      expect(document.querySelector("[role='tooltip']")?.textContent).toContain(
        "Edit kitchen",
      );
    });
    expect(button?.getAttribute("aria-describedby")).toBeTruthy();
  });

  it("shows the directly composed step tooltip on keyboard focus", async () => {
    const host = renderIntoBody(() =>
      createComponent(Stepper, {
        steps: [{ label: "Details", icon: HiOutlinePencil }],
        activeStep: 0,
        onStepChange: () => undefined,
        "aria-label": "Example steps",
      }),
    );
    const button = host.querySelector<HTMLButtonElement>(
      'button[aria-label="Details"]',
    );

    expect(button).not.toBeNull();
    expect(host.querySelectorAll("button button")).toHaveLength(0);
    button?.focus();

    await vi.waitFor(() => {
      expect(document.activeElement).toBe(button);
      expect(document.querySelector("[role='tooltip']")?.textContent).toContain(
        "Details",
      );
    });
    expect(button?.getAttribute("aria-current")).toBe("step");
    expect(button?.getAttribute("aria-describedby")).toBeTruthy();
  });
});
