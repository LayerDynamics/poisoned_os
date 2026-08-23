var eventLoop = require("@flipperdevices/fz-sdk/event_loop");
var timerRecords = [];
function onTimer(subscription) {
    var record = null;
    for (let index = 0; index < timerRecords.length; index++) if (timerRecords[index].subscription === subscription) record = timerRecords[index];
    if (!record || !record.active) return undefined;
    var releaseAfterCallback = !record.periodic;
    if (!record.periodic) {
        record.active = false;
        subscription.cancel();
    }
    record.callback();
    if (releaseAfterCallback && __poison_async_release()) eventLoop.stop();
}
function schedule(callback, delay, periodic) {
    if (typeof callback !== "function") return __poison_fail("timer callback must be a function");
    var milliseconds = delay | 0;
    if (milliseconds < 1) milliseconds = 1;
    var timer = eventLoop.timer(periodic ? "periodic" : "oneshot", milliseconds);
    var handle = { timer: timer, subscription: null, active: true, periodic: periodic, callback: callback };
    __poison_async_acquire();
    var subscription = eventLoop.subscribe(timer, onTimer);
    handle.subscription = subscription;
    timerRecords.push(handle);
    return handle;
}
function clearTimer(handle) {
    if (handle && handle.active && handle.subscription) {
        handle.active = false;
        handle.subscription.cancel();
        if (__poison_async_release()) eventLoop.stop();
    }
}
function setTimeoutImpl(callback, delay) { return schedule(callback, delay, false); }
function setIntervalImpl(callback, delay) { return schedule(callback, delay, true); }
module.exports = { setTimeout: setTimeoutImpl, setInterval: setIntervalImpl, clearTimeout: clearTimer, clearInterval: clearTimer };
