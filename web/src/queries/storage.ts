import { createQuery } from "@tanstack/solid-query";
import { fallbackStorage, loadStorage } from "../lib/api";

export function storageQuery() {
  return createQuery(() => ({
    queryKey: ["storage"],
    queryFn: ({ signal }) => loadStorage(signal),
    placeholderData: fallbackStorage,
  }));
}
