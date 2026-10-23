import path from "node:path";

import tailwindcss from "@tailwindcss/vite";
import solid from "vite-plugin-solid";
import viteCompression from "vite-plugin-compression";
import { viteSingleFile } from "vite-plugin-singlefile";
import { defineConfig } from "vite";
import { mockRouterApi } from "./src/dev/mock-router-api.ts";

const rootDir = import.meta.dirname;

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
