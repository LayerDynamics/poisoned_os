# ESP transport for on-device JavaScript

The JavaScript `http`, `https`, `net`, and `tls` shims use the Flipper JS
serial module and talk to an attached ESP over USART. Frames are newline
delimited ASCII records with the prefix `POISON-ESP/1 `.

Requests contain `version`, `operation`, and `payload` fields. The supported
operations are `http.request`, `net.connect`, and `net.write`; the payload
contains the request URL or socket endpoint, headers/body, and optional serial
settings. Responses contain `ok` and `payload`; when `ok` is false they also
contain an `error` string. A missing response or malformed prefix is reported
as a transport error by the JavaScript shim.

The Flipper side does not emulate a desktop network stack. The attached ESP
firmware must implement these operations and return one response per request.
The serial channel is closed by `esp_transport.close()` so another Flipper
application can acquire it.
