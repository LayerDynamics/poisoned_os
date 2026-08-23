function addListener(name, listener, context) {
    if (typeof listener !== "function") return __poison_fail("listener must be a function");
    if (!this._events[name]) this._events[name] = [];
    this._events[name].push({ listener: listener, once: false, context: context });
    return this;
}
function addOnceListener(name, listener, context) {
    if (typeof listener !== "function") return __poison_fail("listener must be a function");
    if (!this._events[name]) this._events[name] = [];
    this._events[name].push({ listener: listener, once: true, context: context });
    return this;
}
function emit(name, a, b, c, d) {
        var listeners = this._events[name];
        if (!listeners) return false;
        var copy = [];
        for (let item = 0; item < listeners.length; item++) copy.push(listeners[item]);
        var kept = [];
        for (let i = 0; i < copy.length; i++) {
            copy[i].listener(a, b, c, d);
            if (!copy[i].once) kept.push(copy[i]);
        }
        this._events[name] = kept;
        return copy.length > 0;
}
function removeListener(name, listener) {
        var listeners = this._events[name];
        if (!listeners) return this;
        var kept = [];
        for (let i = 0; i < listeners.length; i++) if (listeners[i].listener !== listener) kept.push(listeners[i]);
        this._events[name] = kept;
        return this;
}
function removeAllListeners(name) {
    if (name === undefined) { this._events = {}; }
    else { this._events[name] = undefined; }
    return this;
}
function listenerCount(name) { return this._events[name] ? this._events[name].length : 0; }
function EventEmitter(target) {
    var emitter = target || {};
    emitter._events = {};
    emitter.on = addListener;
    emitter.addListener = addListener;
    emitter.once = addOnceListener;
    emitter.emit = emit;
    emitter.removeListener = removeListener;
    emitter.off = removeListener;
    emitter.removeAllListeners = removeAllListeners;
    emitter.listenerCount = listenerCount;
    return emitter;
}

module.exports = { EventEmitter: EventEmitter };
