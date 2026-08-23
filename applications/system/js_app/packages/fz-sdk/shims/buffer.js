var base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
var hex = "0123456789abcdef";

function base64Value(character) {
    for (let index = 0; index < base64.length; index++) if (base64[index] === character) return index;
    return -1;
}

function viewOf(value) {
    if (value && value._poisonBuffer) return Uint8Array(value.buffer);
    if (value && value.buffer) return Uint8Array(value.buffer);
    return Uint8Array(value);
}

function encodeBase64(bytes) {
    var output = "";
    for (let index = 0; index < bytes.length; index += 3) {
        var a = bytes[index];
        var b = index + 1 < bytes.length ? bytes[index + 1] : 0;
        var c = index + 2 < bytes.length ? bytes[index + 2] : 0;
        output += base64[(a >> 2) & 63];
        output += base64[((a & 3) << 4) | ((b >> 4) & 15)];
        output += index + 1 < bytes.length ? base64[((b & 15) << 2) | ((c >> 6) & 3)] : "=";
        output += index + 2 < bytes.length ? base64[c & 63] : "=";
    }
    return output;
}

function decodeBase64(text) {
    var count = 0;
    for (let index = 0; index < text.length; index++) if (base64Value(text[index]) >= 0) count++;
    var bytes = Uint8Array((count * 6) >> 3);
    var bits = 0, value = 0, output = 0;
    for (let index = 0; index < text.length; index++) {
        if (text[index] === "=") break;
        var digit = base64Value(text[index]);
        if (digit < 0) {
            if (text[index] !== " " && text[index] !== "\n" && text[index] !== "\r" && text[index] !== "\t") return __poison_fail("invalid base64 input");
        } else {
            value = (value << 6) | digit;
            bits += 6;
            if (bits >= 8) { bits -= 8; bytes[output++] = (value >> bits) & 255; }
        }
    }
    return bytes.buffer;
}

function bufferToString(encoding) {
    var bytes = Uint8Array(this.buffer);
    encoding = encoding || "utf8";
    if (encoding === "base64") return encodeBase64(bytes);
    var output = "";
    if (encoding === "hex") {
        for (let index = 0; index < bytes.length; index++) output += hex[(bytes[index] >> 4) & 15] + hex[bytes[index] & 15];
        return output;
    }
    if (encoding !== "utf8" && encoding !== "utf-8" && encoding !== "ascii") return __poison_fail("unsupported Buffer encoding");
    for (let index = 0; index < bytes.length; index++) output += chr(bytes[index]);
    return output;
}

function bufferSlice(start, end) {
    var bytes = Uint8Array(this.buffer);
    start = start === undefined ? 0 : start;
    end = end === undefined ? bytes.length : end;
    var output = Uint8Array(end - start);
    for (let index = start; index < end; index++) output[index - start] = bytes[index];
    return wrap(output.buffer);
}

function wrap(arrayBuffer) {
    var bytes = Uint8Array(arrayBuffer);
    var result = { _poisonBuffer: true, buffer: arrayBuffer, length: bytes.length, byteLength: bytes.length };
    for (let index = 0; index < bytes.length; index++) result[index] = bytes[index];
    result.toString = bufferToString;
    result.slice = bufferSlice;
    return result;
}

function from(value, encoding) {
    if (typeof value === "string") {
        if (encoding === "base64") return wrap(decodeBase64(value));
        var bytes = Uint8Array(value.length);
        for (let index = 0; index < value.length; index++) bytes[index] = value.charCodeAt(index);
        return wrap(bytes.buffer);
    }
    if (value && value._poisonBuffer) return wrap(value.buffer);
    if (value && typeof value.length === "number" && value.byteLength === undefined) {
        var bytes = Uint8Array(value.length);
        for (let index = 0; index < value.length; index++) bytes[index] = value[index];
        return wrap(bytes.buffer);
    }
    return wrap(viewOf(value).buffer);
}

function alloc(length, fill) {
    var bytes = Uint8Array(length);
    if (fill !== undefined) for (let index = 0; index < length; index++) bytes[index] = fill;
    return wrap(bytes.buffer);
}

function concat(values) {
    var length = 0;
    for (let index = 0; index < values.length; index++) length += values[index].length;
    var bytes = Uint8Array(length), offset = 0;
    for (let index = 0; index < values.length; index++) {
        var source = viewOf(values[index]);
        for (let cursor = 0; cursor < source.length; cursor++) bytes[offset++] = source[cursor];
    }
    return wrap(bytes.buffer);
}

var BufferApi = {
    from: from,
    alloc: alloc,
    concat: concat,
    isBuffer: function(value) { return !!(value && value._poisonBuffer); },
    byteLength: function(value) { return from(value).length; },
};
module.exports = { Buffer: BufferApi };
