function decoderWrite(buffer) {
    if (typeof buffer === "string") return buffer;
    var source = buffer && buffer.buffer ? buffer.buffer : buffer;
    var bytes = Uint8Array(source);
    var result = "";
    for (let i = 0; i < bytes.length; i++) result += chr(bytes[i]);
    return result;
}
function StringDecoder(encoding) {
    if (encoding && encoding.toLowerCase() !== "utf8" && encoding.toLowerCase() !== "utf-8") return __poison_fail("only utf8 is supported");
    return { write: decoderWrite, end: decoderWrite };
}
module.exports = { StringDecoder: StringDecoder };
