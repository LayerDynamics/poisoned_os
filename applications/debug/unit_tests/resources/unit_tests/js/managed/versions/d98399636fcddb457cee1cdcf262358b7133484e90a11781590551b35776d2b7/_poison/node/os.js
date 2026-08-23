var flipper = require("@flipperdevices/fz-sdk/flipper");
module.exports = {
    platform: function() { return "flipper"; },
    arch: function() { return "arm"; },
    hostname: function() { return flipper.getName(); },
    homedir: function() { return "/ext"; },
    tmpdir: function() { return "/ext/.tmp"; },
    EOL: "\n",
    type: function() { return "PoisonedOS"; },
    release: function() { return flipper.firmwareVendor; },
};
