var path = require("node:path");
if(path.join("poison", "ready") !== "poison/ready") throw new Error("builtin failed");
print("managed builtin ok");
