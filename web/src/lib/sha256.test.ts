import { describe, expect, it } from "vitest";
import { Sha256, sha256FileHex } from "./sha256";

describe("sha256", () => {
  // Positive: verifies the browser-side OTA digest helper against standard SHA-256
  // vectors used to catch regressions in the pure TypeScript implementation.
  it("matches standard test vectors", async () => {
    await expect(sha256FileHex(new Blob([""]))).resolves.toBe(
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    );
    await expect(sha256FileHex(new Blob(["abc"]))).resolves.toBe(
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    );
  });

  // Positive: hashes a larger OTA candidate in chunks and reports monotonic
  // progress so the upload UI can show honest browser-side work.
  it("streams large files and reports monotonic progress", async () => {
    const payload = new Uint8Array(150_000);
    payload.fill(0x61);
    const progress: number[] = [];

    const digest = await sha256FileHex(new Blob([payload]), (bytes) => {
      progress.push(bytes);
    });

    expect(digest).toMatch(/^[0-9a-f]{64}$/);
    expect(progress.length).toBeGreaterThan(1);
    expect(progress.at(-1)).toBe(payload.length);
    expect(progress).toEqual([...progress].sort((left, right) => left - right));
  });

  // Positive: exercises the SHA-256 padding branch where finalization needs a
  // second block, comparing against WebCrypto as an independent oracle.
  it("handles final padding that needs a second block", async () => {
    const payload = new Uint8Array(56);
    payload.fill(0x61);
    const expected = Array.from(
      new Uint8Array(await crypto.subtle.digest("SHA-256", payload)),
      (byte) => byte.toString(16).padStart(2, "0"),
    ).join("");

    await expect(sha256FileHex(new Blob([payload]))).resolves.toBe(expected);
  });

  it("stops hashing when the owning operation is aborted", async () => {
    const controller = new AbortController();
    controller.abort(new Error("file replaced"));

    await expect(
      sha256FileHex(
        new Blob([new Uint8Array(128_000)]),
        undefined,
        controller.signal,
      ),
    ).rejects.toThrow("file replaced");
  });

  // Negative: rejects updates after finalization so a digest cannot accidentally
  // be mutated after the firmware metadata has been prepared.
  it("does not allow updates after finalization", () => {
    const hasher = new Sha256();
    hasher.update(new Uint8Array([1, 2, 3]));
    expect(hasher.digestHex()).toMatch(/^[0-9a-f]{64}$/);
    expect(() => hasher.update(new Uint8Array([4]))).toThrow(
      "SHA-256 digest already finalized",
    );
  });

  // Positive/negative: repeated finalization is idempotent for display/copy flows,
  // but updating afterward remains forbidden.
  it("returns a stable digest when finalized more than once", () => {
    const hasher = new Sha256();
    hasher.update(new Uint8Array([1, 2, 3]));
    const first = hasher.digestHex();
    const second = hasher.digestHex();
    expect(second).toBe(first);
    expect(() => hasher.update(new Uint8Array([4]))).toThrow(
      "SHA-256 digest already finalized",
    );
  });
});
