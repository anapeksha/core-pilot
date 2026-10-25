import { afterEach, expect, it, vi } from "vitest";
import { accessToken, authHeaders, clearAccess, unlockAccess } from "./auth";

afterEach(() => {
  clearAccess();
  vi.unstubAllGlobals();
  vi.useRealTimers();
});
it("keeps a bounded bearer in RAM and expires it", async () => {
  vi.useFakeTimers();
  const token = "a".repeat(64);
  vi.stubGlobal(
    "fetch",
    vi
      .fn()
      .mockResolvedValue(
        new Response(JSON.stringify({ token, expires_in: 28800 })),
      ),
  );
  await unlockAccess("0123456789abcdef");
  expect(authHeaders()).toEqual({ Authorization: `Bearer ${token}` });
  vi.advanceTimersByTime(28800000);
  expect(accessToken()).toBeNull();
});
it.each([
  ["incorrect", "The access code is incorrect."],
  ["expired", "The access code has expired."],
  ["attempts_exhausted", "Access attempts are exhausted."],
  ["unavailable", "Access is temporarily unavailable."],
])("reports %s without replaying the mutation", async (code, message) => {
  const fetcher = vi.fn().mockResolvedValue(
    new Response(JSON.stringify({ error: { code, message: "ignored" } }), {
      status: 401,
      headers: { "Content-Type": "application/json" },
    }),
  );
  vi.stubGlobal("fetch", fetcher);
  await expect(unlockAccess("bad")).rejects.toThrow(message);
  expect(fetcher).toHaveBeenCalledTimes(1);
  expect(authHeaders()).toEqual({});
});
