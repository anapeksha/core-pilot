import { createQuery } from "@tanstack/solid-query";
import { loadRooms, type RoomsResponse } from "../lib/api";

export const fallbackRooms: RoomsResponse = {
  rooms: [],
  detail: "Create rooms to organize devices by location.",
};

export function roomsQuery() {
  return createQuery(() => ({
    queryKey: ["rooms"],
    queryFn: ({ signal }) => loadRooms(signal),
    placeholderData: fallbackRooms,
  }));
}
