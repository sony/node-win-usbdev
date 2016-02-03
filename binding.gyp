{
  "targets": [
    {
      "target_name": "usb_dev",
      "sources": [ "usb_dev.cc" ],
      "include_dirs" : [
          "<!(node -e \"require('nan')\")"
      ],
      "libraries": [
          "-lsetupapi"
      ]
    }
  ]
}
