var eventLoop = require("@flipperdevices/fz-sdk/event_loop");
function schedule(callback, delay, periodic) {
    if (typeof callback !== "function") throw new TypeError("timer callback must be a function");
    var timer = eventLoop.timer(periodic ? "periodic" : "oneshot", Math.max(1, delay | 0));
    var handle = { timer: timer, subscription: null, active: true };
    __poison_async_acquire();
    var subscription = eventLoop.subscribe(timer, function(subscription) {
        if (!periodic && handle.active) {
            handle.active = false;
            subscription.cancel();
            if (__poison_async_release()) eventLoop.stop();
        }
        callback();
    });
    handle.subscription = subscription;
    return handle;
}
function clearTimer(handle) {
    if (handle && handle.active && handle.subscription) {
        handle.active = false;
        handle.subscription.cancel();
        if (__poison_async_release()) eventLoop.stop();
    }
}
module.exports = { setTimeout: function(callback, delay) { return schedule(callback, delay, false); }, setInterval: function(callback, delay) { return schedule(callback, delay, true); }, clearTimeout: clearTimer, clearInterval: clearTimer };
