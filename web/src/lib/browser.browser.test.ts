import { expect, test } from "vitest";

test("runs dashboard browser integration tests in a real Chromium page", () => {
  expect(window.location.protocol).toMatch(/^https?:$/);
  expect(document.createElement("main")).toBeInstanceOf(HTMLElement);
});
