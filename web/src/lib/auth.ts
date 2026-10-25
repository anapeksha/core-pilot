import { createSignal } from "solid-js";

const [token, setToken] = createSignal<string | null>(null);
export const accessToken = token;
let expiry: ReturnType<typeof setTimeout> | undefined;

export async function initializeAccess(): Promise<void> {
  const response = await fetch("/api/v1/auth", {
    credentials: "omit",
    headers: { Accept: "application/json" },
  });
  if (!response.ok) throw new Error("Could not check device access.");
  const status: unknown = await response.json();
  if (
    status &&
    typeof status === "object" &&
    "authentication_required" in status &&
    status.authentication_required === false
  )
    setToken("0".repeat(64));
}
export function clearAccess() {
  clearTimeout(expiry);
  setToken(null);
}
export function authHeaders(): Record<string, string> {
  const current = token();
  return current ? { Authorization: `Bearer ${current}` } : {};
}
export async function unlockAccess(code: string): Promise<void> {
  const response = await fetch("/api/v1/auth/session", {
    method: "POST",
    credentials: "omit",
    headers: { "Content-Type": "application/json", Accept: "application/json" },
    body: JSON.stringify({ code }),
  });
  if (!response.ok)
    throw new Error("Access denied. Check the code or restart the device.");
  const value: unknown = await response.json();
  if (
    !value ||
    typeof value !== "object" ||
    !("token" in value) ||
    typeof value.token !== "string" ||
    !/^[0-9a-f]{64}$/.test(value.token) ||
    !("expires_in" in value) ||
    value.expires_in !== 28800
  )
    throw new Error("Invalid access response");
  clearAccess();
  setToken(value.token);
  expiry = setTimeout(clearAccess, 28800 * 1000);
}
