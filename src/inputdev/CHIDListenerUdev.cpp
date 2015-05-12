#include "inputdev/IHIDListener.hpp"
#include "inputdev/CDeviceFinder.hpp"
#include <libudev.h>
#include <string.h>
#include <signal.h>
#include <thread>

namespace boo
{

static udev* UDEV_INST = NULL;
udev* GetUdev()
{
    if (!UDEV_INST)
        UDEV_INST = udev_new();
    return UDEV_INST;
}

class CHIDListenerUdev final : public IHIDListener
{
    CDeviceFinder& m_finder;
    
    udev_monitor* m_udevMon;
    std::thread* m_udevThread;
    bool m_udevRunning;
    bool m_scanningEnabled;
    
    static void deviceConnected(CHIDListenerUdev* listener,
                                udev_device* device)
    {
        if (!listener->m_scanningEnabled)
            return;

        /* Filter to USB/BT */
        const char* dt = udev_device_get_devtype(device);
        CDeviceToken::TDeviceType type;
        if (!strcmp(dt, "usb_device"))
            type = CDeviceToken::DEVTYPE_USB;
        else if (!strcmp(dt, "bluetooth_device"))
            type = CDeviceToken::DEVTYPE_BLUETOOTH;
        else
            return;

        /* Prevent redundant registration */
        const char* devPath = udev_device_get_syspath(device);
        if (listener->m_finder._hasToken(devPath))
            return;

        int vid = 0, pid = 0;
        udev_list_entry* attrs = udev_device_get_properties_list_entry(device);
#if 0
        udev_list_entry* att = NULL;
        udev_list_entry_foreach(att, attrs)
        {
            const char* name = udev_list_entry_get_name(att);
            const char* val = udev_list_entry_get_value(att);
            fprintf(stderr, "%s %s\n", name, val);
        }
        fprintf(stderr, "\n\n");
#endif

        udev_list_entry* vide = udev_list_entry_get_by_name(attrs, "ID_VENDOR_ID");
        if (vide)
            vid = strtol(udev_list_entry_get_value(vide), NULL, 16);

        udev_list_entry* pide = udev_list_entry_get_by_name(attrs, "ID_MODEL_ID");
        if (pide)
            pid = strtol(udev_list_entry_get_value(pide), NULL, 16);

        const char* manuf = NULL;
        udev_list_entry* manufe = udev_list_entry_get_by_name(attrs, "ID_VENDOR");
        if (manufe)
            manuf = udev_list_entry_get_value(manufe);

        const char* product = NULL;
        udev_list_entry* producte = udev_list_entry_get_by_name(attrs, "ID_MODEL");
        if (producte)
            product = udev_list_entry_get_value(producte);

        if (!listener->m_finder._insertToken(CDeviceToken(type, vid, pid, manuf, product, devPath)))
        {
            /* Matched-insertion failed; see if generic HID interface is available */
            udev_list_entry* devInterfaces = NULL;
            if (type == CDeviceToken::DEVTYPE_USB)
                devInterfaces = udev_list_entry_get_by_name(attrs, "ID_USB_INTERFACES");
            else if (type == CDeviceToken::DEVTYPE_BLUETOOTH)
                devInterfaces = udev_list_entry_get_by_name(attrs, "ID_BLUETOOTH_INTERFACES");
            if (devInterfaces)
            {
                const char* interfacesStr = udev_list_entry_get_value(devInterfaces);
                if (strstr(interfacesStr, ":030104") || /* HID / GenericDesktop / Joystick */
                    strstr(interfacesStr, ":030105"))   /* HID / GenericDesktop / Gamepad */
                {
                    udev_enumerate* hidEnum = udev_enumerate_new(UDEV_INST);
                    udev_enumerate_add_match_parent(hidEnum, device);
                    udev_enumerate_add_match_subsystem(hidEnum, "hid");
                    udev_enumerate_scan_devices(hidEnum);
                    udev_list_entry* hidEnt = udev_enumerate_get_list_entry(hidEnum);
                    if (hidEnt)
                    {
                        const char* hidPath = udev_list_entry_get_name(hidEnt);
                        if (!listener->m_finder._hasToken(hidPath))
                            listener->m_finder._insertToken(CDeviceToken(CDeviceToken::DEVTYPE_GENERICHID,
                                                                         vid, pid, manuf, product, hidPath));
                    }
                    udev_enumerate_unref(hidEnum);
                }
            }
        }
    }
    
    static void deviceDisconnected(CHIDListenerUdev* listener,
                                   udev_device* device)
    {
        const char* devPath = udev_device_get_syspath(device);
        listener->m_finder._removeToken(devPath);
    }

    static void _udevProc(CHIDListenerUdev* listener)
    {
        udev_monitor_enable_receiving(listener->m_udevMon);
        int fd = udev_monitor_get_fd(listener->m_udevMon);
        while (listener->m_udevRunning)
        {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            select(fd+1, &fds, NULL, NULL, NULL);
            udev_device* dev = udev_monitor_receive_device(listener->m_udevMon);
            if (dev)
            {
                const char* action = udev_device_get_action(dev);
                if (!strcmp(action, "add"))
                    deviceConnected(listener, dev);
                else if (!strcmp(action, "remove"))
                    deviceDisconnected(listener, dev);
                udev_device_unref(dev);
            }
        }
    }

public:
    CHIDListenerUdev(CDeviceFinder& finder)
    : m_finder(finder)
    {
        
        /* Setup hotplug events */
        m_udevMon = udev_monitor_new_from_netlink(GetUdev(), "udev");
        if (!m_udevMon)
            throw std::runtime_error("unable to init udev_monitor");
        udev_monitor_filter_add_match_subsystem_devtype(m_udevMon, "usb", "usb_device");
        udev_monitor_filter_add_match_subsystem_devtype(m_udevMon, "bluetooth", "bluetooth_device");
        udev_monitor_filter_update(m_udevMon);

        /* Initial HID Device Add */
        m_scanningEnabled = true;
        scanNow();
        m_scanningEnabled = false;

        /* Start hotplug thread */
        m_udevRunning = true;
        m_udevThread = new std::thread(_udevProc, this);
        
    }
    
    ~CHIDListenerUdev()
    {
        m_udevRunning = false;
        //raise(SIGINT);
        pthread_kill(m_udevThread->native_handle(), SIGINT);
        m_udevThread->join();
        delete m_udevThread;
        udev_monitor_unref(m_udevMon);
    }
    
    /* Automatic device scanning */
    bool startScanning()
    {
        m_scanningEnabled = true;
        return true;
    }
    bool stopScanning()
    {
        m_scanningEnabled = false;
        return true;
    }
    
    /* Manual device scanning */
    bool scanNow()
    {
        udev_enumerate* uenum = udev_enumerate_new(GetUdev());
        udev_enumerate_add_match_subsystem(uenum, "usb");
        udev_enumerate_add_match_property(uenum, "DEVTYPE", "usb_device");
        udev_enumerate_add_match_subsystem(uenum, "bluetooth");
        udev_enumerate_add_match_property(uenum, "DEVTYPE", "bluetooth_device");
        udev_enumerate_scan_devices(uenum);
        udev_list_entry* uenumList = udev_enumerate_get_list_entry(uenum);
        udev_list_entry* uenumItem;
        udev_list_entry_foreach(uenumItem, uenumList)
        {
            const char* devPath = udev_list_entry_get_name(uenumItem);
            udev_device* dev = udev_device_new_from_syspath(UDEV_INST, devPath);
            if (dev)
                deviceConnected(this, dev);
            udev_device_unref(dev);
        }
        udev_enumerate_unref(uenum);
        return true;
    }
    
};

IHIDListener* IHIDListenerNew(CDeviceFinder& finder)
{
    return new CHIDListenerUdev(finder);
}

}
