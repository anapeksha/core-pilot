import { createHash } from "node:crypto";
import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";

import tailwindcss from "@tailwindcss/vite";
import solid from "vite-plugin-solid";
import viteCompression from "vite-plugin-compression";
import { viteSingleFile } from "vite-plugin-singlefile";
import { defineConfig, type Plugin } from "vite";
import { mockRouterApi } from "./src/dev/mock-router-api.ts";

const rootDir = import.meta.dirname;

function contentSecurityPolicy(): Plugin {
  return {
    name: "core-pilot-csp",
    async closeBundle() {
      const outputDirectory = path.resolve(rootDir, "../dist");
      const source = await readFile(
        path.join(outputDirectory, "index.html"),
        "utf8",
      );
      const hashes = [
        ...source.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/gi),
      ].map(
        (match) =>
          `'sha256-${createHash("sha256").update(match[1]).digest("base64")}'`,
      );
      if (hashes.length === 0)
        throw new Error("built dashboard contains no inline scripts");
      const policy = `default-src 'none'; script-src ${hashes.join(" ")}; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; font-src 'self'; manifest-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'; object-src 'none'`;
      await writeFile(path.join(outputDirectory, "csp.txt"), policy, "utf8");
    },
  };
}

export default defineConfig((env) => ({
  resolve: {
    alias: {
      "~": path.resolve(rootDir, "src"),
      $lib: path.resolve(rootDir, "src"),
    },
  },
  plugins: [
    ...(env.command !== "build" ? [mockRouterApi()] : []),
    tailwindcss(),
    solid(),
    viteSingleFile(),
    contentSecurityPolicy(),
    viteCompression({
      algorithm: "gzip",
      ext: ".gz",
      deleteOriginFile: true,
      filter: /index\.html$/i,
      threshold: 0,
    }),
  ],
  build: {
    target: "esnext",
    outDir: path.resolve(rootDir, "../dist"),
    emptyOutDir: true,
    assetsInlineLimit: Number.MAX_SAFE_INTEGER,
    cssCodeSplit: false,
    sourcemap: false,
    modulePreload: false,
    minify: "esbuild",
    cssMinify: "lightningcss",
    rolldownOptions: {
      input: { index: "index.html" },
      output: {
        sourcemap: false,
        minify: {
          compress: {
            maxIterations: 3,
            dropConsole: true,
            dropDebugger: true,
            sequences: true,
          },
          mangle: { toplevel: true },
        },
      },
    },
  },
}));
