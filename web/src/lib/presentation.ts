const ROOM_ACCENT_CLASSES = [
  "border-l-sky-400/70",
  "border-l-violet-400/70",
  "border-l-amber-400/70",
  "border-l-emerald-400/70",
] as const;

/** Returns a stable room accent without persisting presentation metadata. */
export function roomAccentClass(
  roomId: string,
): (typeof ROOM_ACCENT_CLASSES)[number] {
  let hash = 0;
  for (const character of roomId)
    hash = (hash * 31 + character.charCodeAt(0)) >>> 0;
  return ROOM_ACCENT_CLASSES[hash % ROOM_ACCENT_CLASSES.length];
}

export { ROOM_ACCENT_CLASSES };
