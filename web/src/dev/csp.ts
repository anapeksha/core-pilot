import { createHash } from "node:crypto";
import { parse, type DefaultTreeAdapterTypes } from "parse5";

export function inlineScriptHashes(source: string): string[] {
  const hashes: string[] = [];
  function visit(node: DefaultTreeAdapterTypes.Node): void {
    if ("tagName" in node && node.tagName === "script") {
      const location = node.sourceCodeLocation;
      if (!node.attrs.some((attribute) => attribute.name === "src")) {
        if (!location?.startTag || !location.endTag)
          throw new Error("built dashboard contains an unclosed inline script");
        // Hash original bytes, not serialized or newline-normalized parser text.
        const content = source.slice(
          location.startTag.endOffset,
          location.endTag.startOffset,
        );
        hashes.push(
          `'sha256-${createHash("sha256").update(content).digest("base64")}'`,
        );
      }
    }
    if ("childNodes" in node) node.childNodes.forEach(visit);
  }
  visit(parse(source, { sourceCodeLocationInfo: true }));
  if (hashes.length === 0)
    throw new Error("built dashboard contains no inline scripts");
  return hashes;
}
