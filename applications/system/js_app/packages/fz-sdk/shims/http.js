var EventEmitter = require("./events.js").EventEmitter;
var transport = require("./esp_transport.js");

function ClientResponse(payload) {
    var self = EventEmitter();
    self.statusCode = payload.statusCode;
    self.headers = payload.headers || {};
    self.body = payload.body || "";
    self.read = function() { return self.body; };
    return self;
}

function request(options, callback) {
    if (typeof options === "string") options = { url: options };
    options = options || {};
    var requestObject = EventEmitter();
    requestObject.end = function(body) {
        var result = transport.request("http.request", {
            method: options.method || "GET", url: options.url || options.path,
            headers: options.headers || {}, body: body || options.body || "",
            serial: options.serial,
        }, options.timeout);
        if (!result.ok) { requestObject.emit("error", result.error); return undefined; }
        var response = ClientResponse(result.payload);
        if (callback) callback(response);
        requestObject.emit("response", response);
    };
    return requestObject;
}

function get(options, callback) { var req = request(options, callback); req.end(); return req; }
module.exports = { request: request, get: get, ClientResponse: ClientResponse };
