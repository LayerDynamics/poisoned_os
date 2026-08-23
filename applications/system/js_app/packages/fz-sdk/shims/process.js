var eventLoop = require("@flipperdevices/fz-sdk/event_loop");
var os = require("./os.js");
var queue = [];
var queued = false;
function drain() { queued = false; var items = queue.splice(0, queue.length); for (let i = 0; i < items.length; i++) items[i](); }
function onDrain(subscription) {
    subscription.cancel();
    drain();
    if (__poison_async_release()) eventLoop.stop();
}
function nextTick(callback) {
    if (typeof callback !== "function") return __poison_fail("nextTick callback must be a function");
    queue.push(callback);
    if (!queued) {
        queued = true;
        __poison_async_acquire();
        var timer = eventLoop.timer("oneshot", 1);
        eventLoop.subscribe(timer, onDrain);
    }
}
module.exports = {
    platform: os.platform(),
    arch: os.arch(),
    version: "poison-mjs-1",
    cwd: function() { return "/ext"; },
    env: {},
    nextTick: nextTick,
    uptime: function() { return 0; },
    exit: function(code) { return __poison_fail("process.exit(" + (code || 0) + ")"); },
};
