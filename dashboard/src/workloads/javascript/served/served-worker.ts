interface ValidationRequest { validationId: number; data: unknown; }

self.addEventListener("message", (event: MessageEvent<ValidationRequest>) => {
  const request = event.data;
  let valid = false;
  if (request && Number.isSafeInteger(request.validationId) && request.validationId >= 0 &&
      typeof request.data === "object" && request.data !== null) {
    try {
      valid = new TextEncoder().encode(JSON.stringify(request.data)).byteLength <= 16 * 1024;
    } catch {
      valid = false;
    }
  }
  self.postMessage({ validationId: request?.validationId, valid, data: valid ? request.data : null });
});
