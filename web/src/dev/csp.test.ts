// @vitest-environment node
import { createHash } from "node:crypto";
import { describe, expect, it } from "vitest";
import { inlineScriptHashes } from "./csp";

function hash(content: string): string {
  return `'sha256-${createHash("sha256").update(content).digest("base64")}'`;
}

describe("inline script CSP hashes", () => {
  it("hashes exact original content, including whitespace, CRLF and Unicode", () => {
    const content = '\r\n  console.log("café 🏠 &amp;");\r\n';
    expect(inlineScriptHashes(`<script>${content}</script>`)).toEqual([
      hash(content),
    ]);
  });

  it("parses mixed-case tags and quoted greater-than signs in attributes", () => {
    expect(
      inlineScriptHashes(
        '<ScRiPt data-label="a > b">first()</sCrIpT ><script type=module>second()</script>',
      ),
    ).toEqual([hash("first()"), hash("second()")]);
  });

  it("ignores fake scripts in comments, attributes, raw text and templates", () => {
    const source = `<!-- <script>comment()</script> -->
      <div title="<script>attribute()</script>"></div>
      <textarea><script>text()</script></textarea>
      <style><script>style()</script></style>
      <template><script>inert()</script></template>
      <script>real()</script>`;
    expect(inlineScriptHashes(source)).toEqual([hash("real()")]);
  });

  it("excludes external scripts, even with inline fallback text", () => {
    expect(
      inlineScriptHashes(
        '<script SRC="/external.js">ignored()</script><script>inline()</script>',
      ),
    ).toEqual([hash("inline()")]);
  });

  it("preserves empty scripts and duplicate hashes", () => {
    expect(inlineScriptHashes("<script></script><script></script>")).toEqual([
      hash(""),
      hash(""),
    ]);
  });

  it("does not mistake script-like tag names for scripts", () => {
    expect(() =>
      inlineScriptHashes("<script-fake>ignored()</script-fake>"),
    ).toThrow("no inline scripts");
  });

  it("rejects documents without inline scripts", () => {
    expect(() =>
      inlineScriptHashes('<script src="/external.js"></script>'),
    ).toThrow("no inline scripts");
  });

  it("rejects unclosed inline scripts", () => {
    expect(() => inlineScriptHashes("<script>unfinished()")).toThrow(
      "unclosed inline script",
    );
  });
});
