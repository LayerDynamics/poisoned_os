var nativeCrypto = require("@flipperdevices/fz-sdk/crypto");

function concat(chunks) {
    var size = 0; for (let i = 0; i < chunks.length; i++) size += chunks[i].byteLength;
    var output = Uint8Array(size); var offset = 0;
    for (let j = 0; j < chunks.length; j++) { var input = Uint8Array(chunks[j]); for (let k = 0; k < input.length; k++) output[offset++] = input[k]; }
    return output.buffer;
}
function bytes(value) {
    if (typeof value !== "string") return value;
    var text = "" + value; var output = Uint8Array(text.length);
    for (let i = 0; i < text.length; i++) output[i] = text.charCodeAt(i);
    return output.buffer;
}

function hashFactory(key) {
    var chunks = [];
    return {
        update: function(data) { chunks.push(bytes(data)); return this; },
        digest: function() { return key ? nativeCrypto.hmacSha256(key, concat(chunks)) : nativeCrypto.sha256(concat(chunks)); },
    };
}

module.exports = {
    randomBytes: nativeCrypto.randomBytes,
    hkdfSync: nativeCrypto.hkdfSync,
    createHash: function(algorithm) { if (algorithm !== "sha256") return __poison_fail("only sha256 is supported"); return hashFactory(null); },
    createHmac: function(algorithm, key) { if (algorithm !== "sha256" && algorithm !== "hmac-sha256") return __poison_fail("only sha256 is supported"); return hashFactory(bytes(key)); },
};
