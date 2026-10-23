import { createComponent } from "solid-js";
import { render } from "solid-js/web";
import { afterEach, describe, expect, it } from "vitest";

import "~/app.css";
import { ActivitySkeleton } from "./Activity";

let dispose: (() => void) | undefined;

afterEach(() => {
  dispose?.();
  dispose = undefined;
  document.body.replaceChildren();
});

describe("Activity loading state", () => {
  it("renders visible event rows and filter cards while activity loads", () => {
    const host = document.createElement("div");
    document.body.append(host);
    dispose = render(() => createComponent(ActivitySkeleton, {}), host);

    const status = host.querySelector<HTMLElement>("[role='status']");
    const rows = host.querySelectorAll("[data-activity-skeleton-row]");
    const filters = host.querySelectorAll("[data-activity-skeleton-filter]");
    const placeholders = host.querySelectorAll<HTMLElement>(
      "[data-activity-skeleton-row] [data-visible]",
    );

    expect(status?.textContent).toContain("Loading activity");
    expect(status?.getAttribute("aria-busy")).toBe("true");
    expect(rows).toHaveLength(4);
    expect(filters).toHaveLength(3);
    expect(placeholders.length).toBeGreaterThan(0);

    for (const placeholder of placeholders) {
      expect(placeholder.dataset.visible).toBe("true");
      expect(placeholder.classList.contains("bg-muted")).toBe(true);
      expect([...placeholder.classList]).toContainEqual(
        expect.stringMatching(/^h-/),
      );
    }
  });
});
