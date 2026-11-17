import { createComponent, createSignal } from "solid-js";
import { render } from "solid-js/web";
import { afterEach, describe, expect, it, vi } from "vitest";

import "~/app.css";
import { Skeleton } from "./skeleton";

let dispose: (() => void) | undefined;

afterEach(() => {
  dispose?.();
  dispose = undefined;
  document.body.replaceChildren();
});

function mount(props: Parameters<typeof Skeleton>[0] = {}) {
  const host = document.createElement("div");
  host.style.width = "400px";
  document.body.append(host);
  dispose = render(() => createComponent(Skeleton, props), host);
  return host.firstElementChild as HTMLElement;
}

function expectSize(element: HTMLElement, width: number, height: number) {
  const rect = element.getBoundingClientRect();
  expect(rect.width).toBeCloseTo(width);
  expect(rect.height).toBeCloseTo(height);
  expect(getComputedStyle(element).width).toBe(`${width}px`);
  expect(getComputedStyle(element).height).toBe(`${height}px`);
}

describe("Skeleton sizing", () => {
  it("lets height and percentage width classes size an empty skeleton", () => {
    const element = mount({ class: "h-3 w-1/2" });

    expectSize(element, 200, 12);
    expect(element.style.height).toBe("");
    expect(element.style.width).toBe("");
    expect(element.classList.contains("bg-muted/80")).toBe(true);
    expect(getComputedStyle(element).borderTopLeftRadius).toBe("8px");
  });

  it("preserves numeric height, width and radius over classes", () => {
    const element = mount({
      class: "h-3 w-1/2",
      height: 24,
      width: 80,
      radius: 9,
    });

    expectSize(element, 80, 24);
    expect(getComputedStyle(element).borderTopLeftRadius).toBe("9px");
  });

  it("allows a class width alongside numeric height", () => {
    expectSize(mount({ class: "w-1/2", height: 20 }), 200, 20);
  });

  it("allows a class height alongside numeric width", () => {
    expectSize(mount({ class: "h-3", width: 60 }), 60, 12);
  });

  it("uses height for both circle dimensions, regardless of width or radius", () => {
    const element = mount({
      class: "h-3 w-1/2",
      circle: true,
      height: 32,
      width: 80,
      radius: 9,
    });

    expectSize(element, 32, 32);
    expect(getComputedStyle(element).borderTopLeftRadius).toBe("9999px");
  });

  it.each([
    { height: "18px", width: "25%", "border-radius": "2px" },
    "height:18px;width:25%;border-radius:2px",
  ])("gives explicit style precedence: %j", (style) => {
    const element = mount({
      class: "h-3 w-1/2",
      circle: true,
      height: 32,
      width: 80,
      radius: 9,
      style,
    });

    expectSize(element, 100, 18);
    expect(getComputedStyle(element).borderTopLeftRadius).toBe("2px");
  });

  it("supports zero numeric dimensions", () => {
    expectSize(mount({ class: "h-3 w-1/2", height: 0, width: 0 }), 0, 0);
  });

  it("reactively restores class sizing when numeric dimensions are removed", async () => {
    const [size, setSize] = createSignal<number | undefined>(32);
    const element = mount({
      class: "h-3 w-1/2",
      get height() {
        return size();
      },
      get width() {
        return size();
      },
    });

    expectSize(element, 32, 32);
    setSize(undefined);
    await vi.waitFor(() => expectSize(element, 200, 12));
  });
});

describe("Skeleton animation", () => {
  it("retains primitive visibility and animation attributes and pulse styling", () => {
    const element = mount({ class: "h-3 w-1/2" });

    expect(element.dataset.visible).toBe("true");
    expect(element.dataset.animate).toBe("true");
    expect(getComputedStyle(element).animationName).toBe("pulse");
    if (matchMedia("(prefers-reduced-motion: reduce)").matches) {
      expect(getComputedStyle(element).animationDuration).toBe("1e-05s");
      expect(getComputedStyle(element).animationIterationCount).toBe("1");
    }
  });

  it("disables animation and visibility attributes when requested", () => {
    const element = mount({ animate: false, visible: false, class: "h-3" });

    expect(element.hasAttribute("data-animate")).toBe(false);
    expect(element.hasAttribute("data-visible")).toBe(false);
    expect(getComputedStyle(element).animationName).toBe("none");
  });
});
