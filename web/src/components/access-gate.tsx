import {
  HiOutlineArrowRight,
  HiOutlineDevicePhoneMobile,
  HiOutlineHome,
  HiOutlineLockClosed,
} from "solid-icons/hi";
import { createSignal, onMount, Show, type ParentProps } from "solid-js";
import { Button } from "~/components/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "~/components/card";
import { Spinner } from "~/components/spinner";
import {
  TextField,
  TextFieldDescription,
  TextFieldInput,
  TextFieldLabel,
} from "~/components/text-field";
import {
  AccessError,
  accessToken,
  initializeAccess,
  unlockAccess,
  type BootstrapState,
} from "~/lib/auth";
import { queryClient } from "~/lib/queryClient";

export default function AccessGate(props: ParentProps) {
  const ProtectedContent = () => <>{props.children}</>;
  const [code, setCode] = createSignal("");
  const [busy, setBusy] = createSignal(false);
  const [checking, setChecking] = createSignal(true);
  const [error, setError] = createSignal("");
  const [bootstrapState, setBootstrapState] =
    createSignal<BootstrapState>("unavailable");

  onMount(async () => {
    try {
      setBootstrapState(await initializeAccess());
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
      if (error instanceof AccessError) {
        setError(error.message);
        if (
          error.code === "expired" ||
          error.code === "attempts_exhausted" ||
          error.code === "unavailable"
        )
          setBootstrapState(error.code);
      } else {
        setError(error instanceof Error ? error.message : "Access failed");
      }
    } finally {
      setBusy(false);
    }
  }

  return (
    <>
      {!checking() && accessToken() ? (
        <ProtectedContent />
      ) : (
        <main class="relative grid min-h-screen place-items-center overflow-hidden bg-background px-4 py-10 sm:px-6">
          <div
            class="pointer-events-none absolute inset-0 opacity-60"
            aria-hidden="true"
          >
            <div class="absolute -left-24 -top-24 size-72 rounded-full bg-primary/8 blur-3xl" />
            <div class="absolute -bottom-32 -right-20 size-80 rounded-full bg-primary/6 blur-3xl" />
            <div class="absolute inset-0 bg-[radial-gradient(circle_at_center,hsl(var(--border)/0.22)_1px,transparent_1px)] bg-size-[28px_28px] mask-[linear-gradient(to_bottom,black,transparent_80%)]" />
          </div>

          <div class="relative w-full max-w-md">
            <div class="mb-6 flex items-center justify-center gap-3">
              <span class="brand-orb size-11!" aria-hidden="true">
                <HiOutlineHome size={24} />
              </span>
              <div>
                <p class="text-sm font-medium leading-none">Core Pilot</p>
                <p class="mt-1 text-xs text-muted-foreground">
                  Local home intelligence
                </p>
              </div>
            </div>

            <Card class="border-border/80 bg-card/95 shadow-2xl shadow-black/20 backdrop-blur">
              <CardHeader class="space-y-4 pb-5 text-center">
                <div class="mx-auto grid size-12 place-items-center rounded-full bg-secondary">
                  <HiOutlineLockClosed
                    class="size-5 text-foreground"
                    aria-hidden="true"
                  />
                </div>
                <div class="space-y-2">
                  <CardTitle class="text-2xl">Unlock your dashboard</CardTitle>
                  <CardDescription class="mx-auto max-w-sm leading-relaxed">
                    Confirm physical access to this Core Pilot before managing
                    your home.
                  </CardDescription>
                </div>
              </CardHeader>

              <CardContent>
                <Show
                  when={!checking()}
                  fallback={
                    <div
                      class="flex min-h-44 flex-col items-center justify-center gap-3 text-muted-foreground"
                      role="status"
                    >
                      <Spinner />
                      <p class="text-sm">Checking device access…</p>
                    </div>
                  }
                >
                  <Show
                    when={bootstrapState() === "open"}
                    fallback={
                      <div class="space-y-4 rounded-lg border bg-muted/30 p-4 text-center">
                        <p class="font-medium">
                          {bootstrapState() === "expired"
                            ? "Access code expired"
                            : bootstrapState() === "attempts_exhausted"
                              ? "Access attempts exhausted"
                              : bootstrapState() === "consumed"
                                ? "Access code already used"
                                : "Access temporarily unavailable"}
                        </p>
                        <p class="text-sm leading-relaxed text-muted-foreground">
                          {bootstrapState() === "consumed"
                            ? "This physical code has already created a browser session."
                            : "Use Generate new access code on the CoreS3 touchscreen. Remote clients cannot reopen access."}
                        </p>
                      </div>
                    }
                  >
                    <form class="space-y-5" onSubmit={submit}>
                      <TextField
                        value={code()}
                        onChange={(value) => setCode(value.toLowerCase())}
                        validationState={error() ? "invalid" : "valid"}
                      >
                        <TextFieldLabel>Bootstrap code</TextFieldLabel>
                        <TextFieldInput
                          id="access-code"
                          type="password"
                          autocomplete="one-time-code"
                          autocapitalize="none"
                          spellcheck={false}
                          required
                          pattern="[0-9a-f]{16}"
                          maxLength={16}
                          placeholder="16-character code"
                          class="h-12 font-mono tracking-[0.18em]"
                        />
                        <TextFieldDescription class="mt-1.5 flex items-start gap-2 text-left leading-relaxed">
                          <HiOutlineDevicePhoneMobile
                            class="mt-0.5 size-4 shrink-0"
                            aria-hidden="true"
                          />
                          Find the code on the CoreS3 display. It expires five
                          minutes after startup.
                        </TextFieldDescription>
                      </TextField>

                      <Show when={error()}>
                        {(message) => (
                          <div
                            class="rounded-md border border-destructive/40 bg-destructive/10 px-3 py-2 text-sm text-destructive-foreground"
                            role="alert"
                          >
                            {message()}
                          </div>
                        )}
                      </Show>

                      <Button
                        type="submit"
                        size="lg"
                        class="w-full"
                        disabled={busy() || code().length !== 16}
                      >
                        <Show
                          when={!busy()}
                          fallback={
                            <>
                              <Spinner />
                              Unlocking…
                            </>
                          }
                        >
                          Unlock dashboard
                          <HiOutlineArrowRight aria-hidden="true" />
                        </Show>
                      </Button>
                    </form>
                  </Show>
                </Show>
              </CardContent>
            </Card>

            <p class="mt-5 text-center text-xs leading-relaxed text-muted-foreground">
              Your access session stays in this browser tab and expires after
              eight hours.
            </p>
          </div>
        </main>
      )}
    </>
  );
}
