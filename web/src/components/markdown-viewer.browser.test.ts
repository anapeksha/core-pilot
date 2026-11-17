import { createComponent, createSignal } from "solid-js";
import { render } from "solid-js/web";
import { afterEach, describe, expect, it, vi } from "vitest";
import "~/app.css";
import { MarkdownViewer, type MarkdownViewerProps } from "./markdown-viewer";

let dispose: (() => void) | undefined;
afterEach(() => {
  dispose?.();
  document.body.replaceChildren();
});

function mount(props: MarkdownViewerProps, width = 320) {
  const host = document.createElement("div");
  host.style.cssText = `display:flex;width:${width}px;max-width:100%`;
  document.body.append(host);
  dispose = render(() => createComponent(MarkdownViewer, props), host);
  return host.firstElementChild as HTMLElement;
}

describe("MarkdownViewer", () => {
  it("renders semantic GFM with working typography and same-tab named links", () => {
    const root = mount({
      markdown: `# Heading

## Subheading

A **strong** and *emphasized* paragraph with \`inline\` and ~~deleted~~.

- First
  1. Nested ordered
  2. Second
- Last

3. Ordered
4. Next

> Quoted

\`\`\`js
const value = "<safe>";
\`\`\`

| Name | Value |
| --- | --- |
| Entry | 42 |

[Documentation](https://example.com "Help")`,
      class: "custom-viewer",
    });
    for (const selector of [
      "h1",
      "h2",
      "strong",
      "em",
      "p code",
      "del",
      "ul > li > ol",
      "ol[start='3']",
      "blockquote",
      "pre > code",
      "table > thead",
      "tbody td",
    ]) {
      expect(root.querySelector(selector)).not.toBeNull();
    }
    expect(root.querySelector("pre code")?.textContent).toContain("<safe>");
    expect(root.classList.contains("custom-viewer")).toBe(true);
    expect(getComputedStyle(root.querySelector("h1")!).fontSize).not.toBe(
      getComputedStyle(root).fontSize,
    );
    expect(getComputedStyle(root.querySelector("ul")!).listStyleType).toBe(
      "disc",
    );
    const link = root.querySelector("a")!;
    expect(link.textContent).toBe("Documentation");
    expect(link.getAttribute("rel")).toBe("noopener noreferrer");
    expect(link.getAttribute("referrerpolicy")).toBe("no-referrer");
    expect(link.hasAttribute("target")).toBe(false);
  });

  it.each([280, 400, 640])(
    "contains wide tables, code and long text at %ipx",
    (width) => {
      const long = "unbroken".repeat(100);
      const root = mount(
        {
          markdown: `${long}\n\n\`\`\`\n${long}\n\`\`\`\n\n| Header | Other |\n| --- | --- |\n| ${long} | ${long} |`,
        },
        width,
      );
      expect(root.getBoundingClientRect().width).toBeLessThanOrEqual(width);
      expect(root.scrollWidth).toBeLessThanOrEqual(width + 1);
      expect(document.documentElement.scrollWidth).toBeLessThanOrEqual(
        document.documentElement.clientWidth,
      );
      const table = root.querySelector("table")!;
      expect(getComputedStyle(table).display).toBe("table");
      expect(table.hasAttribute("role")).toBe(false);
      expect(table.hasAttribute("tabindex")).toBe(false);
      expect(table.querySelector("thead th")).not.toBeNull();
      for (const selector of ["pre", ".markdown-viewer-table"]) {
        const element = root.querySelector<HTMLElement>(selector)!;
        expect(getComputedStyle(element).overflowX).toBe("auto");
        expect(element.scrollWidth).toBeGreaterThan(element.clientWidth);
        expect(element.tabIndex).toBe(0);
        expect(element.getAttribute("role")).toBe("group");
        expect(element.getAttribute("aria-label")).toBe(
          selector === "pre" ? "Code block" : "Table",
        );
        element.focus();
        expect(document.activeElement).toBe(element);
        expect(getComputedStyle(element).outlineStyle).not.toBe("none");
        element.scrollLeft = 80;
        expect(element.scrollLeft).toBeGreaterThan(0);
        expect(element.getBoundingClientRect().width).toBeLessThanOrEqual(
          width,
        );
      }
    },
  );

  it("rejects scripts, events, unsafe URLs, CSS, forms and clobbering HTML", async () => {
    const root = mount({
      markdown: `before

<script>document.body.dataset.markdownExecuted = 'yes'</script>
<style>body { display: none }</style>
<iframe src="https://example.invalid/frame"></iframe>
<form id="location"><input name="cookie"></form>
<svg onload="document.body.dataset.markdownExecuted='yes'"></svg>
<a id="location" name="cookie" style="color:red" onclick="alert(1)" href="javascript:alert(1)">raw</a>

[script](javascript:alert%281%29)
[data](data:text/html,evil)
[encoded](java&#x73;cript:alert%281%29)
[invalid](https://[)
[local](/help)

![<img src=x onerror=alert(1)>](https://example.invalid/image)

\`<script>inert</script>\``,
    });
    await new Promise((resolve) => setTimeout(resolve, 50));
    expect(document.body.dataset.markdownExecuted).toBeUndefined();
    expect(
      root.querySelector("script,style,iframe,form,input,svg,img,object,embed"),
    ).toBeNull();
    for (const element of root.querySelectorAll("*")) {
      for (const attribute of element.attributes) {
        expect(attribute.name).not.toMatch(/^(on|style$|id$|name$|src)/i);
      }
    }
    expect(
      [...root.querySelectorAll("a[href]")].map((a) => a.getAttribute("href")),
    ).toEqual(["/help"]);
    expect(root.querySelector("code")?.textContent).toBe(
      "<script>inert</script>",
    );
  });

  it("never fetches Markdown or raw HTML images, including reference images", async () => {
    const url = `${location.origin}/markdown-image-must-not-fetch`;
    const root = mount({
      markdown: `![Visible alt](${url}?markdown)\n\n![Reference][image]\n\n[image]: ${url}?reference\n\n<img src="${url}?raw" srcset="${url}?srcset 2x" onerror="alert(1)">\n<picture><source srcset="${url}?source"><img src="${url}?picture"></picture>`,
    });
    await new Promise((resolve) => setTimeout(resolve, 100));
    expect(root.textContent).toContain("Visible alt");
    expect(root.textContent).toContain("Reference");
    expect(root.querySelector("img,picture,source")).toBeNull();
    expect(
      performance
        .getEntriesByType("resource")
        .filter((entry) => entry.name.startsWith(url)),
    ).toHaveLength(0);
  });

  it("reactively replaces content, including empty Markdown", async () => {
    const [markdown, setMarkdown] = createSignal("# First");
    const root = mount({
      get markdown() {
        return markdown();
      },
    });
    setMarkdown("**Updated**");
    await vi.waitFor(() =>
      expect(root.querySelector("strong")?.textContent).toBe("Updated"),
    );
    expect(root.querySelector("h1")).toBeNull();
    setMarkdown("");
    await vi.waitFor(() => expect(root.innerHTML).toBe(""));
  });
});
