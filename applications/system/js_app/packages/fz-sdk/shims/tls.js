var net = require("./net.js");
function connect(options, callback) {
    options = options || {};
    options.tls = true;
    return net.createConnection(options.port, options.host, callback);
}
module.exports = { connect: connect, TLSSocket: net.Socket };
