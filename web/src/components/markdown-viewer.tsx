import { createMemo } from "solid-js";
import DOMPurify from "dompurify";
import { Marked } from "marked";

export interface MarkdownViewerProps {
  markdown: string;
  class?: string;
}

function escapeText(text: string): string {
  return text
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

const parser = new Marked({
  gfm: true,
  renderer: {
    // Discard raw HTML before any browser parser can discover resource URLs.
    html: () => "",
    image: ({ text }) => escapeText(text),
  },
});

function renderMarkdown(markdown: string): string {
  const fragment = DOMPurify.sanitize(
    parser.parse(markdown, { async: false }),
    {
      ALLOWED_TAGS: [
        "p",
        "br",
        "hr",
        "h1",
        "h2",
        "h3",
        "h4",
        "h5",
        "h6",
        "ul",
        "ol",
        "li",
        "strong",
        "em",
        "del",
        "code",
        "pre",
        "blockquote",
        "table",
        "thead",
        "tbody",
        "tr",
        "th",
        "td",
        "a",
      ],
      ALLOWED_ATTR: ["href", "title", "start"],
      ALLOW_DATA_ATTR: false,
      ALLOW_ARIA_ATTR: false,
      RETURN_DOM_FRAGMENT: true,
    },
  );
  for (const link of fragment.querySelectorAll("a")) {
    const href = link.getAttribute("href");
    if (href) {
      try {
        const url = new URL(href, window.location.href);
        if (url.protocol !== "http:" && url.protocol !== "https:") {
          link.removeAttribute("href");
        }
      } catch {
        link.removeAttribute("href");
      }
    }
    link.setAttribute("rel", "noopener noreferrer");
    link.setAttribute("referrerpolicy", "no-referrer");
  }
  for (const pre of fragment.querySelectorAll("pre")) {
    pre.tabIndex = 0;
    pre.setAttribute("role", "group");
    pre.setAttribute("aria-label", "Code block");
  }
  for (const table of fragment.querySelectorAll("table")) {
    const wrapper = document.createElement("div");
    wrapper.className = "markdown-viewer-table";
    wrapper.tabIndex = 0;
    wrapper.setAttribute("role", "group");
    wrapper.setAttribute("aria-label", "Table");
    table.replaceWith(wrapper);
    wrapper.append(table);
  }
  const container = document.createElement("div");
  container.append(fragment);
  return container.innerHTML;
}

/** Callers enforce their existing prompt/memory byte limits before rendering. */
export function MarkdownViewer(props: MarkdownViewerProps) {
  const html = createMemo(() => renderMarkdown(props.markdown));
  return (
    <div
      class={`markdown-viewer prose prose-sm prose-invert ${props.class ?? ""}`}
      innerHTML={html()}
    />
  );
}
