import { createComponent, type JSX } from "solid-js";
import { render } from "solid-js/web";
import { HiOutlinePencil } from "solid-icons/hi";
import { afterEach, describe, expect, it, vi } from "vitest";

import "~/app.css";
import {
  Sheet,
  SheetContent,
  SheetDescription,
  SheetTitle,
  SheetTrigger,
} from "~/components/sheet";
import { Stepper } from "~/components/stepper";
import {
  Tooltip,
  TooltipContent,
  TooltipIconButton,
  TooltipTrigger,
} from "~/components/tooltip";

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

  it("keeps hover and focus tooltips accessible across modal reopenings", async () => {
    const host = renderIntoBody(() =>
      createComponent(Sheet, {
        get children() {
          return [
            createComponent(SheetTrigger, { children: "Open tooltip sheet" }),
            createComponent(SheetContent, {
              get children() {
                const focusTarget = document.createElement("button");
                focusTarget.textContent = "Focus before hovering";
                return [
                  createComponent(SheetTitle, { children: "Tooltip sheet" }),
                  createComponent(SheetDescription, {
                    children: "Modal tooltip accessibility regression",
                  }),
                  focusTarget,
                  createComponent(Tooltip, {
                    openDelay: 0,
                    get children() {
                      return [
                        createComponent(TooltipTrigger, {
                          "aria-label": "Automation limits",
                          children: "Limits",
                        }),
                        createComponent(TooltipContent, {
                          children:
                            "One trigger and up to eight ordered actions",
                        }),
                      ];
                    },
                  }),
                ];
              },
            }),
          ];
        },
      }),
    );
    const openButton = host.querySelector<HTMLButtonElement>("button")!;

    for (let opening = 0; opening < 2; opening++) {
      openButton.click();
      await vi.waitFor(() => {
        expect(document.querySelector('[role="dialog"]')).not.toBeNull();
        expect(host.closest('[aria-hidden="true"]')).not.toBeNull();
      });
      const dialog = document.querySelector('[role="dialog"]')!;
      const button = dialog.querySelector<HTMLButtonElement>(
        'button[aria-label="Automation limits"]',
      )!;

      for (const interaction of ["hover", "focus"]) {
        if (interaction === "hover") {
          button.dispatchEvent(
            new PointerEvent("pointerenter", {
              bubbles: true,
              pointerType: "mouse",
            }),
          );
        } else {
          button.focus();
        }
        await vi.waitFor(() => {
          expect(document.querySelector('[role="tooltip"]')?.textContent).toBe(
            "One trigger and up to eight ordered actions",
          );
        });

        // Wait for the real modal observer's deferred hiding, not just portal mount.
        const outsideProbe = document.createElement("div");
        document.body.append(outsideProbe);
        await vi.waitFor(() => {
          expect(outsideProbe.getAttribute("aria-hidden")).toBe("true");
        });
        const tooltip = document.querySelector('[role="tooltip"]')!;
        expect(tooltip.closest('[aria-hidden="true"]')).toBeNull();
        expect(button.getAttribute("aria-describedby")).toBe(tooltip.id);
        if (interaction === "focus")
          expect(document.activeElement).toBe(button);
        outsideProbe.remove();

        button.dispatchEvent(
          new PointerEvent("pointerleave", {
            bubbles: true,
            pointerType: "mouse",
          }),
        );
        button.dispatchEvent(
          new KeyboardEvent("keydown", { key: "Escape", bubbles: true }),
        );
        dialog.querySelector<HTMLButtonElement>("button")!.focus();
        await vi.waitFor(() => {
          expect(document.querySelector('[role="tooltip"]')).toBeNull();
        });
      }

      dialog
        .querySelector<HTMLButtonElement>('button[aria-label="Close panel"]')!
        .click();
      await vi.waitFor(() => {
        expect(document.querySelector('[role="dialog"]')).toBeNull();
        expect(host.closest('[aria-hidden="true"]')).toBeNull();
      });
    }
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
