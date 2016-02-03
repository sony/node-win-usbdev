var VID = 0x054C;
var PID = 0x0B94;

var addon = require("./build/Release/usb_dev.node");
var path = addon.getPath(VID, PID);
console.log(path);

