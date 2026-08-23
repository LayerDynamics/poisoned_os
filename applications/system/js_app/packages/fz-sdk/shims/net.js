var EventEmitter = require("./events.js").EventEmitter;
var transport = require("./esp_transport.js");

function Socket(options) {
    var self = EventEmitter();
    self.options = options || {};
    self.connected = false;
    self.connect = function(port, host, callback) {
        if (typeof host === "function") { callback = host; host = undefined; }
        self.options.port = port || self.options.port;
        self.options.host = host || self.options.host;
        var result = transport.request("net.connect", self.options);
        if (!result.ok) { self.emit("error", result.error); }
        else { self.connected = true; self.emit("connect"); if (callback) { callback(); } }
        return self;
    };
    self.write = function(data, callback) {
        var result = transport.request("net.write", { connection: self.options, data: data });
        if (!result.ok) { self.emit("error", result.error); return false; }
        if (callback) callback();
        return true;
    };
    self.end = function(data) { if (data) self.write(data); self.connected = false; self.emit("close"); };
    return self;
}
function createConnection(port, host, callback) { return Socket().connect(port, host, callback); }
module.exports = { Socket: Socket, createConnection: createConnection, connect: createConnection };
