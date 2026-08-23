interface WorkerEvent { waitUntil(promise: Promise<unknown>): void; }
interface WorkerFetchEvent extends WorkerEvent {
  readonly request: Request;
  respondWith(response: Promise<Response>): void;
}
interface WorkerScope {
  readonly clients: { claim(): Promise<void> };
  addEventListener(type: "install" | "activate", listener: (event: WorkerEvent) => void): void;
  addEventListener(type: "fetch", listener: (event: WorkerFetchEvent) => void): void;
}

const worker = self as unknown as WorkerScope;
const CACHE = "poisonedos-dashboard-v1";
worker.addEventListener("install", (event) => {
  event.waitUntil(caches.open(CACHE).then((cache) => cache.addAll(["/", "/index.html", "/manifest.webmanifest"])));
});
worker.addEventListener("activate", (event) => { event.waitUntil(worker.clients.claim()); });
worker.addEventListener("fetch", (event) => {
  event.respondWith(caches.match(event.request).then((cached) => cached ?? fetch(event.request).then((response) => {
    if (event.request.method === "GET" && response.ok) void caches.open(CACHE).then((cache) => cache.put(event.request, response.clone()));
    return response;
  })));
});
