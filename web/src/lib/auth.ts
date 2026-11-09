import { createSignal } from "solid-js";

export type BootstrapState =
  "open" | "consumed" | "expired" | "attempts_exhausted" | "unavailable";
export type AccessErrorCode =
  | "incorrect"
  | "expired"
  | "attempts_exhausted"
  | "unavailable"
  | "invalid_request"
  | "rate_limited";

export class AccessError extends Error {
  constructor(
    readonly code: AccessErrorCode,
    message: string,
  ) {
    super(message);
  }
}

const sessionSeconds = 1800;

const [token, setToken] = createSignal<string | null>(null);
const [granted, setGranted] = createSignal(false);
export const accessToken = token;
export const accessGranted = granted;
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
    credentials: "same-origin",
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
    setGranted(true);
    return "consumed";
  }
  if (
    status &&
    typeof status === "object" &&
    "bootstrap_state" in status &&
    isBootstrapState(status.bootstrap_state)
  ) {
    if (
      "session_authenticated" in status &&
      status.session_authenticated === true
    ) {
      setGranted(true);
      expiry = setTimeout(clearAccess, sessionSeconds * 1000);
    }
    return status.bootstrap_state;
  }
  return "open";
}

export function clearAccess() {
  clearTimeout(expiry);
  expiry = undefined;
  setToken(null);
  setGranted(false);
}

export function authHeaders(): Record<string, string> {
  const current = token();
  return current ? { Authorization: `Bearer ${current}` } : {};
}

export async function logoutAccess(): Promise<void> {
  const current = token();
  try {
    if (granted() || current)
      await fetch("/api/v1/auth/logout", {
        method: "POST",
        credentials: "same-origin",
        headers: {
          Accept: "application/json",
          ...(current ? { Authorization: `Bearer ${current}` } : {}),
          "Content-Type": "application/json",
        },
        body: "{}",
      });
  } catch {
    // Logout is best effort; local access must still be cleared.
  } finally {
    clearAccess();
  }
}

const accessMessages: Record<AccessErrorCode, string> = {
  incorrect: "The access code is incorrect.",
  expired: "The access code has expired. Generate a new code on the CoreS3.",
  attempts_exhausted:
    "Access attempts are exhausted. Generate a new code on the CoreS3.",
  unavailable: "Access is temporarily unavailable. Try again on the CoreS3.",
  invalid_request: "Enter the complete 16-character access code.",
  rate_limited: "Too many access attempts. Wait before trying again.",
};

export async function unlockAccess(code: string): Promise<void> {
  const response = await fetch("/api/v1/auth/session", {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json", Accept: "application/json" },
    body: JSON.stringify({ code, mode: "cookie" }),
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
    !("authenticated" in value) ||
    value.authenticated !== true ||
    !("expires_in" in value) ||
    value.expires_in !== sessionSeconds
  )
    throw new AccessError("unavailable", accessMessages.unavailable);
  clearAccess();
  setGranted(true);
  expiry = setTimeout(clearAccess, sessionSeconds * 1000);
}
