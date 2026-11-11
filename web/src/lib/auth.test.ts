import { afterEach, expect, it, vi } from "vitest";
import { queryClient } from "./queryClient";
import {
  accessGranted,
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
  queryClient.clear();
});
it("creates a bounded same-origin cookie session and expires local access", async () => {
  vi.useFakeTimers();
  const fetcher = vi
    .fn()
    .mockResolvedValue(
      new Response(JSON.stringify({ authenticated: true, expires_in: 1800 })),
    );
  vi.stubGlobal("fetch", fetcher);

  await unlockAccess("0123456789abcdef");

  expect(fetcher).toHaveBeenCalledWith("/api/v1/auth/session", {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json", Accept: "application/json" },
    body: JSON.stringify({ code: "0123456789abcdef", mode: "cookie" }),
  });
  expect(accessGranted()).toBe(true);
  expect(authHeaders()).toEqual({});
  vi.advanceTimersByTime(1800000);
  expect(accessGranted()).toBe(false);
  expect(accessToken()).toBeNull();
});

it("clears sensitive query data when access expires", async () => {
  vi.useFakeTimers();
  vi.stubGlobal(
    "fetch",
    vi
      .fn()
      .mockResolvedValue(
        new Response(JSON.stringify({ authenticated: true, expires_in: 1800 })),
      ),
  );
  queryClient.setQueryData(["sensitive"], { secret: "cached" });

  await unlockAccess("0123456789abcdef");
  vi.advanceTimersByTime(1_800_000);

  expect(queryClient.getQueryData(["sensitive"])).toBeUndefined();
});

it("revokes the cookie session before clearing local access", async () => {
  const fetcher = vi
    .fn()
    .mockResolvedValueOnce(
      new Response(JSON.stringify({ authenticated: true, expires_in: 1800 })),
    )
    .mockResolvedValueOnce(new Response(JSON.stringify({ accepted: true })));
  vi.stubGlobal("fetch", fetcher);

  await unlockAccess("0123456789abcdef");
  queryClient.setQueryData(["sensitive"], { secret: "cached" });
  await logoutAccess();

  expect(queryClient.getQueryData(["sensitive"])).toBeUndefined();
  expect(fetcher).toHaveBeenNthCalledWith(2, "/api/v1/auth/logout", {
    method: "POST",
    credentials: "same-origin",
    headers: {
      Accept: "application/json",
      "Content-Type": "application/json",
    },
    body: "{}",
  });
  expect(accessGranted()).toBe(false);
  expect(accessToken()).toBeNull();
});

it("clears local access when server logout transport fails", async () => {
  const fetcher = vi
    .fn()
    .mockResolvedValueOnce(
      new Response(JSON.stringify({ authenticated: true, expires_in: 1800 })),
    )
    .mockRejectedValueOnce(new TypeError("offline"));
  vi.stubGlobal("fetch", fetcher);

  await unlockAccess("0123456789abcdef");
  await expect(logoutAccess()).resolves.toBeUndefined();
  expect(accessGranted()).toBe(false);
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
          session_authenticated: true,
        }),
      ),
    ),
  );

  await expect(initializeAccess()).resolves.toBe("consumed");
  expect(accessGranted()).toBe(true);
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
  ["rate_limited", "Too many access attempts."],
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
