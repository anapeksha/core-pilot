import { createComponent } from "solid-js";
import { render } from "solid-js/web";
import { afterEach, expect, test, vi } from "vitest";
import ConnectionStatus from "./connection-status";
import { queryClient } from "~/lib/queryClient";

let dispose: (() => void) | undefined;

afterEach(() => {
  dispose?.();
  document.body.replaceChildren();
  vi.restoreAllMocks();
});

test("announces offline and performs one refresh when reconnecting", async () => {
  let finishRefresh: (() => void) | undefined;
  const refresh = vi.spyOn(queryClient, "refetchQueries").mockReturnValue(
    new Promise<void>((resolve) => {
      finishRefresh = resolve;
    }),
  );
  const root = document.createElement("div");
  document.body.append(root);
  dispose = render(() => createComponent(ConnectionStatus, {}), root);

  window.dispatchEvent(new Event("offline"));
  expect(root.textContent).toContain("Offline");

  window.dispatchEvent(new Event("online"));
  window.dispatchEvent(new Event("online"));
  expect(root.textContent).toContain("Reconnecting");
  expect(refresh).toHaveBeenCalledTimes(1);
  expect(refresh).toHaveBeenCalledWith({ type: "active" });

  finishRefresh?.();
  await vi.waitFor(() => expect(root.textContent).toContain("Online"));
});
