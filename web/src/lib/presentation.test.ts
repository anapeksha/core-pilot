import { describe, expect, it } from "vitest";

import { ROOM_ACCENT_CLASSES, roomAccentClass } from "./presentation";

describe("room presentation", () => {
  it("keeps room accents stable and within the shared palette", () => {
    const accent = roomAccentClass("living-room");
    expect(roomAccentClass("living-room")).toBe(accent);
    expect(ROOM_ACCENT_CLASSES).toContain(accent);
  });

  it("does not depend on collection order", () => {
    const rooms = ["kitchen", "bedroom", "office"];
    const before = Object.fromEntries(
      rooms.map((id) => [id, roomAccentClass(id)]),
    );
    rooms.reverse();
    const after = Object.fromEntries(
      rooms.map((id) => [id, roomAccentClass(id)]),
    );
    expect(after).toEqual(before);
  });
});
