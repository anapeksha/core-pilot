import { fileURLToPath } from "node:url";
import solid from "vite-plugin-solid";
import { defineConfig } from "vitest/config";

export default defineConfig({
  plugins: [solid()],
  resolve: {
    alias: {
      "~": fileURLToPath(new URL("./src", import.meta.url)),
      $lib: fileURLToPath(new URL("./src", import.meta.url)),
    },
  },
  test: {
    environment: "jsdom",
    include: ["src/**/*.test.ts"],
    exclude: ["src/**/*.browser.test.ts"],
    coverage: {
      include: ["src/lib/**/*.ts"],
      exclude: ["src/lib/routes.ts", "src/**/*.test.ts"],
      thresholds: { statements: 80, branches: 70, functions: 70, lines: 80 },
    },
  },
});
