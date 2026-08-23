export interface ServiceAddressRegistration {
  name: string;
  url: string;
  ownerPid: number;
  ttlMs: number;
}

export interface ServiceAddressLease {
  name: string;
  url: string;
  ownerPid: number;
  expiresAtMs: number;
}

const SERVICE_NAME_PATTERN = /^[a-z][a-z0-9-]{0,62}$/;
const MAX_TTL_MS = 60 * 60 * 1_000;

function normalizeLocalServiceUrl(value: string): string {
  let url: URL;
  try {
    url = new URL(value);
  } catch {
    throw new Error(`Node service URL is malformed: ${value}`);
  }

  if (url.protocol !== "http:" && url.protocol !== "https:") {
    throw new Error("Node service URL must use HTTP or HTTPS");
  }
  const host = url.hostname.toLowerCase();
  const isIpv4Loopback = /^127(?:\.\d{1,3}){3}$/.test(host);
  if (host !== "localhost" && host !== "::1" && !isIpv4Loopback) {
    throw new Error(`Node service address must be local: ${value}`);
  }
  if (url.username || url.password) {
    throw new Error("Node service URL must not contain credentials");
  }
  if (url.search || url.hash) {
    throw new Error("Node service URL must not contain a query or fragment");
  }

  return url.href;
}

function validateName(name: string): void {
  if (!SERVICE_NAME_PATTERN.test(name)) {
    throw new Error(`Invalid Node service name: ${name}`);
  }
}

function validateOwner(ownerPid: number): void {
  if (!Number.isSafeInteger(ownerPid) || ownerPid <= 0) {
    throw new Error("Node service owner PID must be a positive integer");
  }
}

function validateTtl(ttlMs: number): void {
  if (!Number.isSafeInteger(ttlMs) || ttlMs <= 0 || ttlMs > MAX_TTL_MS) {
    throw new Error(`Node service lease TTL must be between 1 and ${MAX_TTL_MS} milliseconds`);
  }
}

export class ServiceAddressRegistry {
  private readonly leases = new Map<string, ServiceAddressLease>();

  constructor(private readonly now: () => number = Date.now) {}

  register(registration: ServiceAddressRegistration): ServiceAddressLease {
    this.expire();
    validateName(registration.name);
    validateOwner(registration.ownerPid);
    validateTtl(registration.ttlMs);
    const normalizedUrl = normalizeLocalServiceUrl(registration.url);

    if (this.leases.has(registration.name)) {
      throw new Error(`Node service name ${registration.name} is already leased`);
    }
    for (const lease of this.leases.values()) {
      if (lease.url === normalizedUrl) {
        throw new Error(`Node service address ${normalizedUrl} is already leased by ${lease.name}`);
      }
    }

    const lease: ServiceAddressLease = {
      name: registration.name,
      url: normalizedUrl,
      ownerPid: registration.ownerPid,
      expiresAtMs: this.now() + registration.ttlMs,
    };
    this.leases.set(lease.name, lease);
    return { ...lease };
  }

  renew(name: string, ownerPid: number, ttlMs: number): ServiceAddressLease {
    this.expire();
    validateName(name);
    validateOwner(ownerPid);
    validateTtl(ttlMs);
    const lease = this.requireOwnedLease(name, ownerPid);
    lease.expiresAtMs = this.now() + ttlMs;
    return { ...lease };
  }

  release(name: string, ownerPid: number): void {
    this.expire();
    validateName(name);
    validateOwner(ownerPid);
    this.requireOwnedLease(name, ownerPid);
    this.leases.delete(name);
  }

  snapshot(): ServiceAddressLease[] {
    this.expire();
    return [...this.leases.values()].map((lease) => ({ ...lease }));
  }

  private requireOwnedLease(name: string, ownerPid: number): ServiceAddressLease {
    const lease = this.leases.get(name);
    if (!lease) {
      throw new Error(`Node service ${name} does not have an active lease`);
    }
    if (lease.ownerPid !== ownerPid) {
      throw new Error(`Node service ${name} lease belongs to a different owner`);
    }
    return lease;
  }

  private expire(): void {
    const now = this.now();
    for (const [name, lease] of this.leases) {
      if (lease.expiresAtMs <= now) {
        this.leases.delete(name);
      }
    }
  }
}

export { normalizeLocalServiceUrl };
