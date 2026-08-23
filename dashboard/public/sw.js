const CACHE = "poisonedos-dashboard-v1";
self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(CACHE).then((cache) => cache.addAll(["/", "/index.html", "/manifest.webmanifest"])));
});
self.addEventListener("activate", (event) => { event.waitUntil(self.clients.claim()); });
self.addEventListener("fetch", (event) => {
  event.respondWith(caches.match(event.request).then((cached) => cached ?? fetch(event.request).then((response) => {
    if (event.request.method === "GET" && response.ok) void caches.open(CACHE).then((cache) => cache.put(event.request, response.clone()));
    return response;
  })));
});
