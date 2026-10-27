import { describe, expect, it } from "vitest";

import { isOfficialProviderBaseUrl } from "./AiAssistant";

describe("AI provider validation", () => {
  it("accepts only official HTTPS API roots", () => {
    expect(isOfficialProviderBaseUrl("https://api.openai.com/v1")).toBe(true);
    expect(isOfficialProviderBaseUrl("https://api.anthropic.com/v1/")).toBe(
      true,
    );
    expect(isOfficialProviderBaseUrl("http://api.openai.com/v1")).toBe(false);
    expect(isOfficialProviderBaseUrl("https://example.com/v1")).toBe(false);
    expect(isOfficialProviderBaseUrl("https://api.openai.com/v1/chat")).toBe(
      false,
    );
  });
});
