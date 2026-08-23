var serial = require("@flipperdevices/fz-sdk/serial");

var configured = false;
var port = "usart";
var baudRate = 115200;

function setup(options) {
    options = options || {};
    if (!configured) {
        port = options.port || port;
        baudRate = options.baudRate || baudRate;
        serial.setup(port, baudRate);
        configured = true;
    }
}

function request(operation, payload, timeout) {
    setup(payload && payload.serial);
    var frame = { version: 1, operation: operation, payload: payload || {} };
    serial.write("POISON-ESP/1 " + JSON.stringify(frame) + "\n");
    var line = serial.readln(timeout || 5000);
    if (!line) { return { ok: false, error: "ESP transport timeout" }; }
    if (line.indexOf("POISON-ESP/1 ") !== 0) { return { ok: false, error: "Invalid ESP transport response" }; }
    var response = JSON.parse(line.slice(13));
    if (!response.ok) { return { ok: false, error: response.error || "ESP transport request failed" }; }
    return { ok: true, payload: response.payload };
}

function close() {
    if (configured) { serial.end(); configured = false; }
}

module.exports = { setup: setup, request: request, close: close };
