/******************************************************************************

  usb_dev.cc

Description:
    Get a specific usb device path for windows        

Usage:
    ex [in JavaScript code]) 
    
    var VID = 0x054C;
    var PID = 0x0B94;

    var addon = require("./build/Release/usb_dev.node");
    var path = addon.getPath(VID, PID);
    console.log(path);

Knowledge relation:
  Caller Knows :
    NaN, VID/PID

Depends:
  Microsoft Windows API, C++/STL, stdio, stdlib

Copyright:
  Copyright (c) 2016 Sony Corporation.  All rights reserved.
******************************************************************************/


#include <nan.h>

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <regex>

#include <Windows.h>
#include <setupapi.h> // SetupDixxx
#include <Cfgmgr32.h> // CM_Get_xxx
#include <initguid.h> // GUID
#include <usbiodef.h> // GUID


#define INVALID_DEVICE_NUMBER -1

using namespace std;

static vector<string> regex_search(const string& text, const regex& re) {
    vector<string> result;
    smatch m;
    regex_search(text, m, re);
    for (auto && elem : m) {
        result.push_back(elem.str());
    }
    return result;
}

/**
   @param instanceIdに、@param pidと@param vidが、文字列として含まれているかをチェックする関数
*/
static bool isSelectedUsbDevInst(int vid, int pid, const char* instanceId) {
    string text(instanceId);
    regex re( R"(USB\\VID_([0-9A-F]{4})&PID_([0-9A-F]{4})\\)" );
    vector<string> result = regex_search(text, re);

    if (result.size() != 3) { // pidとvidと全体の計3つの文字列がmatchする必要がある
        return false;
    }
    int instVid, instPid;
    sscanf_s(result[1].c_str(), "%x", &instVid);
    sscanf_s(result[2].c_str(), "%x", &instPid);

    if (vid == instVid && pid == instPid) {
        return true;
    }
    return false;
}

static bool isFloppy(char driveLetter) {
    // QueryDosDeviceの取得には、"C:"形式でドライブレターを指定
    char devPath[] = "X:";
    devPath[0] = driveLetter;

    char dosDevName[MAX_PATH] = {};
    if (!::QueryDosDeviceA(devPath, dosDevName, MAX_PATH)) return false;
    return (strstr(dosDevName, "\\Floppy") != NULL);
}


static bool isDisk(char driveLetter) {
    // GetDriveTypeの取得には、"C:\"形式でドライブレターを指定
    char rootPath[] = "X:\\";
    rootPath[0] = driveLetter;

    switch (::GetDriveTypeA(rootPath)) {
    case DRIVE_REMOVABLE:
        if (!isFloppy(driveLetter)) {
            return true;
        } else {
            return false;
        }
    case DRIVE_FIXED:
        return true;
    case DRIVE_CDROM:
    default:
        return false;
    }
}


/**
  DeviceNumberから、該当するdeviceInstanceを返す関数

  [Note]DeviceがDiskの場合のみ呼び出される前提
*/ 
static unsigned long getDevNumByPidVid(int vid, int pid) {

    // Device Information Setの中で、DISK Device Interfaceを検索する
    HDEVINFO hDevInfo = ::SetupDiGetClassDevsA(&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo == INVALID_HANDLE_VALUE)  {
        return INVALID_DEVICE_NUMBER;
    }
    SP_DEVICE_INTERFACE_DATA devIfData = {};
    SP_DEVINFO_DATA devInfoData = {};

    devIfData.cbSize = sizeof(devIfData);

    for (unsigned long index = 0;
        ::SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_DISK, index, &devIfData);
        index++)  {
        unsigned long requiredSize = 0;
        ::SetupDiGetDeviceInterfaceDetailA(hDevInfo, &devIfData, NULL, 0, &requiredSize, NULL);

        vector<char> buf(requiredSize, NULL);
        PSP_DEVICE_INTERFACE_DETAIL_DATA_A pDevIfDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)(&buf.at(0));
        if (requiredSize == 0) continue;
        pDevIfDetailData->cbSize = sizeof(*pDevIfDetailData);
        devInfoData.cbSize = sizeof(devInfoData);

        if (!::SetupDiGetDeviceInterfaceDetailA(hDevInfo, &devIfData, pDevIfDetailData, requiredSize, &requiredSize, &devInfoData)) continue;

        HANDLE hDrive = ::CreateFileA(pDevIfDetailData->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDrive == INVALID_HANDLE_VALUE) continue;

        STORAGE_DEVICE_NUMBER storageDevNum = {};
        unsigned long bytesReturned = 0;
        if (::DeviceIoControl(hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &storageDevNum, sizeof(storageDevNum), &bytesReturned, NULL)) {
            DEVINST devInst = devInfoData.DevInst;
            char devInstId[MAX_PATH] = {};
            if (!::CM_Get_Parent(&devInst, devInst, 0) &&
                !::CM_Get_Device_IDA(devInst, devInstId, MAX_PATH, 0) &&
                isSelectedUsbDevInst(vid, pid, devInstId)) {
                ::CloseHandle(hDrive);
                ::SetupDiDestroyDeviceInfoList(hDevInfo);
                return storageDevNum.DeviceNumber;
            }
        }
        ::CloseHandle(hDrive);
    }
    ::SetupDiDestroyDeviceInfoList(hDevInfo);
    return INVALID_DEVICE_NUMBER;
}


static unsigned long getDevNumByDriveLetter(char driveLetter) {
    char volumeAccessPath[] = "\\\\.\\X:";
    volumeAccessPath[4] = driveLetter;

    unsigned long devNum = INVALID_DEVICE_NUMBER;

    HANDLE hVolume = ::CreateFileA(volumeAccessPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hVolume == INVALID_HANDLE_VALUE) return devNum;

    STORAGE_DEVICE_NUMBER sdn = {};
    unsigned long bytesReturned = 0;
    if (::DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &bytesReturned, NULL)) {
        devNum = sdn.DeviceNumber;
    }
    ::CloseHandle(hVolume);
    return devNum;
}

/**
   pidとvidから、ドライブレターを取得
   "X"
*/
static char getDriveLetter(int vid, int pid) {
    unsigned long devNum = getDevNumByPidVid(vid, pid);
    const unsigned long bufSize = ::GetLogicalDriveStringsA(0, NULL) + sizeof(NULL);
    if (bufSize <= 0) return NULL;
    vector<char> buf(bufSize, NULL);
    if (::GetLogicalDriveStringsA((bufSize - sizeof(NULL)), &buf.at(0)) <= 0) return NULL;
    for (vector<char>::iterator itr = buf.begin(); itr != buf.end(); itr++) {
        if (!isDisk(*itr)) continue;
        if (devNum == getDevNumByDriveLetter(*itr)) {
            return *itr;
        }
    }
    return NULL;
}

/**
   pidとvidから、ドライブパスを取得
   "X:\"
*/
static void getPath(int vid, int pid, unsigned long lpath, char* path) {
    char driveLetter = getDriveLetter(vid, pid);
    char rootPath[] = "X:\\";
    rootPath[0] = driveLetter;
    memcpy_s(path, lpath, rootPath, sizeof(rootPath));
}

void GetPath(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() < 2) {
        Nan::ThrowTypeError("Wrong number of arguments");
        return;
    }

    if (!info[0]->IsNumber() || !info[1]->IsNumber()) {
        Nan::ThrowTypeError("Wrong arguments");
        return;
    }
    int vid = (int)info[0]->NumberValue();
    int pid = (int)info[1]->NumberValue();
    char path[MAX_PATH] = {};
    getPath(vid, pid, MAX_PATH, path);
    info.GetReturnValue().Set(Nan::New(path).ToLocalChecked());
}

void init(v8::Local<v8::Object> exports) {
    exports->Set(Nan::New("getPath").ToLocalChecked(),
        Nan::New<v8::FunctionTemplate>(GetPath)->GetFunction());
}

NODE_MODULE(usb_dev, init)
