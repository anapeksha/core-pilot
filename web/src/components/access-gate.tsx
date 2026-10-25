import { createSignal, onMount, Show, type ParentProps } from "solid-js";
import { accessToken, initializeAccess, unlockAccess } from "~/lib/auth";
import { queryClient } from "~/lib/queryClient";

export default function AccessGate(props: ParentProps) {
  const ProtectedContent = () => <>{props.children}</>;
  const [code, setCode] = createSignal("");
  const [busy, setBusy] = createSignal(false);
  const [checking, setChecking] = createSignal(true);
  const [error, setError] = createSignal("");
  onMount(async () => {
    try {
      await initializeAccess();
    } catch (error) {
      setError(
        error instanceof Error
          ? error.message
          : "Could not check device access.",
      );
    } finally {
      setChecking(false);
    }
  });
  async function submit(event: SubmitEvent) {
    event.preventDefault();
    if (busy()) return;
    setBusy(true);
    setError("");
    const entered = code();
    setCode("");
    try {
      queryClient.clear();
      await unlockAccess(entered);
    } catch (error) {
      setError(error instanceof Error ? error.message : "Access failed");
    } finally {
      setBusy(false);
    }
  }
  return (
    <>
      {!checking() && accessToken() ? (
        <ProtectedContent />
      ) : (
        <main class="mx-auto max-w-md p-8">
          <h1>Core Pilot access</h1>
          <Show when={!checking()} fallback={<p>Checking device access…</p>}>
            <p>
              Enter the physical bootstrap code. It expires five minutes after
              startup. Access stays in this tab only.
            </p>
            <form onSubmit={submit}>
              <label for="access-code">Bootstrap code</label>
              <input
                id="access-code"
                type="password"
                autocomplete="off"
                required
                pattern="[0-9a-f]{16}"
                maxLength={16}
                value={code()}
                onInput={(event) => setCode(event.currentTarget.value)}
              />
              <button type="submit" disabled={busy()}>
                Unlock
              </button>
              <p role="alert">{error()}</p>
            </form>
          </Show>
        </main>
      )}
    </>
  );
}
