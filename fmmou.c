/*

*/

#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#include <CoreFoundation/CoreFoundation.h>

#define kTestMessage        "FM Radio Mouse"
#define kOurVendorID        0x04b4    //Vendor ID of the USB device
#define kOurProductID       0x0001    //Product ID of the USB device

//Global variables
static int			freq;
static IONotificationPortRef    gNotifyPort;
static io_iterator_t            gRawAddedIter;

void getstat(IOUSBDeviceInterface **dev)
{
    kern_return_t               kr;
    IOUSBDevRequest request;
    char devDesc[1024];

    request.bmRequestType = 0x80;
    request.bRequest = kUSBRqGetDescriptor;
    request.wValue = kUSBStringDesc << 8 | 0x78;
    request.wIndex = 0x0409;
    request.wLength = sizeof(devDesc);
    request.pData = &devDesc;
    request.wLenDone = 0;

    kr = (*dev)->DeviceRequest(dev, &request);
    if (kr != kIOReturnSuccess)
    {
            printf("Unable to DeviceRequest: %08x\n", kr);
    }
    int val = (devDesc[4] << 8) | devDesc[5];
    int cfreq = (val - 0x1508) / 8 + 780;
}

void getname(IOUSBDeviceInterface **dev)
{
    kern_return_t               kr;
    IOUSBDevRequest request;
    char devDesc[1024];

    request.bmRequestType = 0x80;
    request.bRequest = kUSBRqGetDescriptor;
    request.wValue = kUSBStringDesc << 8 | 0x02;
    request.wIndex = 0x0409;
    request.wLength = sizeof(devDesc);
    request.pData = &devDesc;
    request.wLenDone = 0;

    kr = (*dev)->DeviceRequest(dev, &request);
    if (kr != kIOReturnSuccess)
    {
            printf("Unable to DeviceRequest: %08x\n", kr);
    }
}

void setfreq(IOUSBDeviceInterface **dev, int f)
{
    kern_return_t               kr;
    IOUSBDevRequest request;
    char devDesc[1024];

    int val = 0x1508 + (f - 780) * 8;

    request.bmRequestType = 0x80;
    request.bRequest = kUSBRqGetDescriptor;
    request.wValue = kUSBStringDesc << 8 | 0x79;
    request.wIndex = 0x0409;
    request.wLength = sizeof(devDesc);
    request.pData = &devDesc;
    request.wLenDone = 0;

    kr = (*dev)->DeviceRequest(dev, &request);
    if (kr != kIOReturnSuccess)
    {
            printf("Unable to DeviceRequest: %08x\n", kr);
    }

    request.bmRequestType = 0x80;
    request.bRequest = kUSBRqGetDescriptor;
    request.wValue = kUSBStringDesc << 8 | (val >> 8);
    request.wIndex = 0x0409;
    request.wLength = sizeof(devDesc);
    request.pData = &devDesc;
    request.wLenDone = 0;

    kr = (*dev)->DeviceRequest(dev, &request);
    if (kr != kIOReturnSuccess)
    {
            printf("Unable to DeviceRequest: %08x\n", kr);
    }

    request.bmRequestType = 0x80;
    request.bRequest = kUSBRqGetDescriptor;
    request.wValue = kUSBStringDesc << 8 | (val & 0xff);
    request.wIndex = 0x0409;
    request.wLength = sizeof(devDesc);
    request.pData = &devDesc;
    request.wLenDone = 0;

    kr = (*dev)->DeviceRequest(dev, &request);
    if (kr != kIOReturnSuccess)
    {
            printf("Unable to DeviceRequest: %08x\n", kr);
    }

}


void RawDeviceAdded(void *refCon, io_iterator_t iterator)
{
    kern_return_t               kr;
    io_service_t                usbDevice;
    IOCFPlugInInterface         **plugInInterface = NULL;
    IOUSBDeviceInterface        **dev = NULL;
    HRESULT                     result;
    SInt32                      score;
    UInt16                      vendor;
    UInt16                      product;
 
    while (usbDevice = IOIteratorNext(iterator))
    {
        //Create an intermediate plug-in
        kr = IOCreatePlugInInterfaceForService(usbDevice,
                    kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
                    &plugInInterface, &score);
        //Don’t need the device object after intermediate plug-in is created
        kr = IOObjectRelease(usbDevice);
        if ((kIOReturnSuccess != kr) || !plugInInterface)
        {
            printf("Unable to create a plug-in (%08x)\n", kr);
            continue;
        }
        //Now create the device interface
        result = (*plugInInterface)->QueryInterface(plugInInterface,
                        CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                        (LPVOID *)&dev);
        //Don’t need the intermediate plug-in after device interface
        //is created
        (*plugInInterface)->Release(plugInInterface);
 
        if (result || !dev)
        {
            printf("Couldn’t create a device interface (%08x)\n",
                                                    (int) result);
            continue;
        }
 
        //Check these values for confirmation
        kr = (*dev)->GetDeviceVendor(dev, &vendor);
        kr = (*dev)->GetDeviceProduct(dev, &product);
        if ((vendor != kOurVendorID) || (product != kOurProductID))
        {
            printf("Found unwanted device (vendor = %d, product = %d)\n",
                    vendor, product);
            (void) (*dev)->Release(dev);
            continue;
        }
 
        //Open the device to change its state
        kr = (*dev)->USBDeviceOpen(dev);
        if (kr != kIOReturnSuccess)
        {
            printf("Unable to open device: %08x\n", kr);
            (void) (*dev)->Release(dev);
            continue;
        }

        //Configure device
        getname(dev);
        setfreq(dev, freq);
        getstat(dev);
 
        //Close this device and release object
        kr = (*dev)->USBDeviceClose(dev);
        kr = (*dev)->Release(dev);
    }
}

int main (int argc, const char *argv[])
{
    mach_port_t             masterPort;
    CFMutableDictionaryRef  matchingDict;
    CFRunLoopSourceRef      runLoopSource;
    kern_return_t           kr;
    SInt32                  usbVendor = kOurVendorID;
    SInt32                  usbProduct = kOurProductID;
 
    // Get command line arguments, if any
    freq = atoi(argv[1]);
 
    //Create a master port for communication with the I/O Kit
    kr = IOMasterPort(MACH_PORT_NULL, &masterPort);
    if (kr || !masterPort)
    {
        printf("ERR: Couldn’t create a master I/O Kit port(%08x)\n", kr);
        return -1;
    }
    //Set up matching dictionary for class IOUSBDevice and its subclasses
    matchingDict = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matchingDict)
    {
        printf("Couldn’t create a USB matching dictionary\n");
        mach_port_deallocate(mach_task_self(), masterPort);
        return -1;
    }
 
    //Add the vendor and product IDs to the matching dictionary.
    //This is the second key in the table of device-matching keys of the
    //USB Common Class Specification
    CFDictionarySetValue(matchingDict, CFSTR(kUSBVendorName),
                        CFNumberCreate(kCFAllocatorDefault,
                                     kCFNumberSInt32Type, &usbVendor));
    CFDictionarySetValue(matchingDict, CFSTR(kUSBProductName),
                        CFNumberCreate(kCFAllocatorDefault,
                                    kCFNumberSInt32Type, &usbProduct));
 
    //To set up asynchronous notifications, create a notification port and
    //add its run loop event source to the program’s run loop
    gNotifyPort = IONotificationPortCreate(masterPort);
    runLoopSource = IONotificationPortGetRunLoopSource(gNotifyPort);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource,
                        kCFRunLoopDefaultMode);
 
    //Retain additional dictionary references because each call to
    //IOServiceAddMatchingNotification consumes one reference
    matchingDict = (CFMutableDictionaryRef) CFRetain(matchingDict);
    matchingDict = (CFMutableDictionaryRef) CFRetain(matchingDict);
    matchingDict = (CFMutableDictionaryRef) CFRetain(matchingDict);
 
    //Now set up two notifications: one to be called when a raw device
    //is first matched by the I/O Kit and another to be called when the
    //device is terminated
    //Notification of first match:
    kr = IOServiceAddMatchingNotification(gNotifyPort,
                    kIOFirstMatchNotification, matchingDict,
                    RawDeviceAdded, NULL, &gRawAddedIter);
    //Iterate over set of matching devices to access already-present devices
    //and to arm the notification
    RawDeviceAdded(NULL, gRawAddedIter);
 
    //Finished with master port
    mach_port_deallocate(mach_task_self(), masterPort);
    masterPort = 0;
 
    //Start the run loop so notifications will be received
//    CFRunLoopRun();
 
    //Because the run loop will run forever until interrupted,
    //the program should never reach this point
    return 0;
}

