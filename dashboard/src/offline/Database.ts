export const DATABASE_NAME = "poisonedos-local-v1";
export const DATABASE_VERSION = 1;

export function openPoisonDatabase(): Promise<IDBDatabase> {
  if (typeof indexedDB === "undefined") return Promise.reject(new Error("IndexedDB is unavailable"));
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DATABASE_NAME, DATABASE_VERSION);
    request.onerror = () => reject(request.error ?? new Error("database open failed"));
    request.onupgradeneeded = () => { for (const store of ["metadata", "mutations", "projects"]) if (!request.result.objectStoreNames.contains(store)) request.result.createObjectStore(store, { keyPath: "id" }); };
    request.onsuccess = () => resolve(request.result);
  });
}
