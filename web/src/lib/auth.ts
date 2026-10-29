import { createSignal } from "solid-js";

export type BootstrapState =
  "open" | "consumed" | "expired" | "attempts_exhausted" | "unavailable";
export type AccessErrorCode =
  | "incorrect"
  | "expired"
  | "attempts_exhausted"
  | "unavailable"
  | "invalid_request";

export class AccessError extends Error {
  constructor(
    readonly code: AccessErrorCode,
    message: string,
  ) {
    super(message);
  }
}

const sessionSeconds = 28800;
const tokenPattern = /^[0-9a-f]{64}$/;

const [token, setToken] = createSignal<string | null>(null);
export const accessToken = token;
let expiry: ReturnType<typeof setTimeout> | undefined;

function isBootstrapState(value: unknown): value is BootstrapState {
  return (
    value === "open" ||
    value === "consumed" ||
    value === "expired" ||
    value === "attempts_exhausted" ||
    value === "unavailable"
  );
}

export async function initializeAccess(): Promise<BootstrapState> {
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
  ) {
    setToken("0".repeat(64));
    return "consumed";
  }
  if (
    status &&
    typeof status === "object" &&
    "bootstrap_state" in status &&
    isBootstrapState(status.bootstrap_state)
  )
    return status.bootstrap_state;
  return "open";
}

export function clearAccess() {
  clearTimeout(expiry);
  expiry = undefined;
  setToken(null);
}

export function authHeaders(): Record<string, string> {
  const current = token();
  return current ? { Authorization: `Bearer ${current}` } : {};
}

const accessMessages: Record<AccessErrorCode, string> = {
  incorrect: "The access code is incorrect.",
  expired: "The access code has expired. Generate a new code on the CoreS3.",
  attempts_exhausted:
    "Access attempts are exhausted. Generate a new code on the CoreS3.",
  unavailable: "Access is temporarily unavailable. Try again on the CoreS3.",
  invalid_request: "Enter the complete 16-character access code.",
};

export async function unlockAccess(code: string): Promise<void> {
  const response = await fetch("/api/v1/auth/session", {
    method: "POST",
    credentials: "omit",
    headers: { "Content-Type": "application/json", Accept: "application/json" },
    body: JSON.stringify({ code }),
  });
  if (!response.ok) {
    let errorCode: AccessErrorCode = "unavailable";
    try {
      const value: unknown = await response.json();
      if (
        value &&
        typeof value === "object" &&
        "error" in value &&
        value.error &&
        typeof value.error === "object" &&
        "code" in value.error &&
        typeof value.error.code === "string" &&
        value.error.code in accessMessages
      )
        errorCode = value.error.code as AccessErrorCode;
    } catch {
      // Use the bounded local fallback below.
    }
    throw new AccessError(errorCode, accessMessages[errorCode]);
  }
  const value: unknown = await response.json();
  if (
    !value ||
    typeof value !== "object" ||
    !("token" in value) ||
    typeof value.token !== "string" ||
    !tokenPattern.test(value.token) ||
    !("expires_in" in value) ||
    value.expires_in !== sessionSeconds
  )
    throw new AccessError("unavailable", accessMessages.unavailable);
  clearAccess();
  setToken(value.token);
  expiry = setTimeout(clearAccess, sessionSeconds * 1000);
}
