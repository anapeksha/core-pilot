import { afterEach, expect, it, vi } from "vitest";
import {
  accessToken,
  authHeaders,
  clearAccess,
  initializeAccess,
  logoutAccess,
  unlockAccess,
} from "./auth";

afterEach(() => {
  clearAccess();
  sessionStorage.clear();
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
        new Response(JSON.stringify({ token, expires_in: 1800 })),
      ),
  );
  await unlockAccess("0123456789abcdef");
  expect(authHeaders()).toEqual({ Authorization: `Bearer ${token}` });
  vi.advanceTimersByTime(1800000);
  expect(accessToken()).toBeNull();
});

it("revokes the server session before clearing the RAM bearer", async () => {
  const token = "d".repeat(64);
  const fetcher = vi
    .fn()
    .mockResolvedValueOnce(
      new Response(JSON.stringify({ token, expires_in: 1800 })),
    )
    .mockResolvedValueOnce(new Response(JSON.stringify({ accepted: true })));
  vi.stubGlobal("fetch", fetcher);

  await unlockAccess("0123456789abcdef");
  await logoutAccess();

  expect(fetcher).toHaveBeenNthCalledWith(2, "/api/v1/auth/logout", {
    method: "POST",
    credentials: "omit",
    headers: {
      Accept: "application/json",
      Authorization: `Bearer ${token}`,
      "Content-Type": "application/json",
    },
    body: "{}",
  });
  expect(accessToken()).toBeNull();
});

it("clears the RAM bearer when server logout transport fails", async () => {
  const token = "e".repeat(64);
  const fetcher = vi
    .fn()
    .mockResolvedValueOnce(
      new Response(JSON.stringify({ token, expires_in: 1800 })),
    )
    .mockRejectedValueOnce(new TypeError("offline"));
  vi.stubGlobal("fetch", fetcher);

  await unlockAccess("0123456789abcdef");
  await expect(logoutAccess()).resolves.toBeUndefined();
  expect(accessToken()).toBeNull();
});

it("does not restore bearer material from browser storage", async () => {
  sessionStorage.setItem("core-pilot-access-token", "b".repeat(64));
  const storedExpiry = String(Date.now() + 60_000);
  sessionStorage.setItem("core-pilot-access-expiry", storedExpiry);
  vi.stubGlobal(
    "fetch",
    vi.fn().mockResolvedValue(
      new Response(
        JSON.stringify({
          authentication_required: true,
          bootstrap_state: "consumed",
        }),
      ),
    ),
  );

  await expect(initializeAccess()).resolves.toBe("consumed");
  expect(authHeaders()).toEqual({});
  expect(sessionStorage.getItem("core-pilot-access-token")).toBe(
    "b".repeat(64),
  );
  expect(sessionStorage.getItem("core-pilot-access-expiry")).toBe(storedExpiry);
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
