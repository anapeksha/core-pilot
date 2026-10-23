import { describe, expect, it } from "vitest";
import { queryClient } from "./queryClient";

describe("query client defaults", () => {
  // Positive/negative: keeps firmware reads stable by default and documents the
  // disabled browser refetch triggers that would overload the embedded server.
  it("keeps dashboard API data stable unless explicitly refreshed", () => {
    expect(queryClient.getDefaultOptions().queries).toMatchObject({
      staleTime: Infinity,
      gcTime: 5 * 60_000,
      retry: false,
      refetchOnMount: false,
      refetchOnReconnect: false,
      refetchOnWindowFocus: false,
    });
    expect(queryClient.getDefaultOptions().queries?.refetchOnMount).not.toBe(
      true,
    );
    expect(
      queryClient.getDefaultOptions().queries?.refetchOnWindowFocus,
    ).not.toBe(true);
  });
});
