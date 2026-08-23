export interface ClientIdentityProvider {
  getOrCreate(): Promise<CryptoKeyPair>;
}

interface StoredIdentity {
  readonly id: "primary";
  readonly privateKey: CryptoKey;
  readonly publicKey: CryptoKey;
}

const DATABASE_NAME = "poisoned-os-client-identity";
const STORE_NAME = "identity";

function generateIdentity(): Promise<CryptoKeyPair> {
  return crypto.subtle.generateKey(
    { name: "ECDSA", namedCurve: "P-256" },
    false,
    ["sign", "verify"],
  );
}

export class BrowserIdentityStore implements ClientIdentityProvider {
  private volatileIdentity: CryptoKeyPair | null = null;

  public async getOrCreate(): Promise<CryptoKeyPair> {
    if (typeof indexedDB === "undefined") {
      this.volatileIdentity ??= await generateIdentity();
      return this.volatileIdentity;
    }

    const database = await this.open();
    try {
      const existing = await this.read(database);
      if (existing) return { privateKey: existing.privateKey, publicKey: existing.publicKey };
      const identity = await generateIdentity();
      await this.write(database, identity);
      return identity;
    } finally {
      database.close();
    }
  }

  private open(): Promise<IDBDatabase> {
    return new Promise((resolve, reject) => {
      const request = indexedDB.open(DATABASE_NAME, 1);
      request.onupgradeneeded = () => {
        if (!request.result.objectStoreNames.contains(STORE_NAME)) {
          request.result.createObjectStore(STORE_NAME, { keyPath: "id" });
        }
      };
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error ?? new Error("client identity database failed to open"));
    });
  }

  private read(database: IDBDatabase): Promise<StoredIdentity | null> {
    return new Promise((resolve, reject) => {
      const transaction = database.transaction(STORE_NAME, "readonly");
      const request = transaction.objectStore(STORE_NAME).get("primary");
      request.onsuccess = () => resolve((request.result as StoredIdentity | undefined) ?? null);
      request.onerror = () => reject(request.error ?? new Error("client identity failed to load"));
    });
  }

  private write(database: IDBDatabase, identity: CryptoKeyPair): Promise<void> {
    return new Promise((resolve, reject) => {
      const transaction = database.transaction(STORE_NAME, "readwrite");
      transaction.objectStore(STORE_NAME).add({ id: "primary", ...identity } satisfies StoredIdentity);
      transaction.oncomplete = () => resolve();
      transaction.onerror = () => reject(transaction.error ?? new Error("client identity failed to persist"));
      transaction.onabort = () => reject(transaction.error ?? new Error("client identity persistence aborted"));
    });
  }
}
