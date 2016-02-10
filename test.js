var VID = 0x054C; // VID for Sony
var PID = 0x0B94; // PID for test device

var addon = require("./build/Release/usb_dev.node");
var path = addon.getPath(VID, PID);
console.log(path);

