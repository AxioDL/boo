#ifndef __IOKITPOINTER_HPP__
#define __IOKITPOINTER_HPP__

#include "CFPointer.hpp"
#include <IOKit/IOTypes.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <utility>

/// A smart pointer that can manage the lifecycle of IOKit objects.
template<typename T>
class IOObjectPointer {
public:
    IOObjectPointer() : storage(0) { }

    IOObjectPointer(T pointer) : storage(toStorageType(pointer)) {
        if (storage) {
            IOObjectRetain(storage);
        }
    }

    IOObjectPointer(const IOObjectPointer & other) : storage(other.storage) {
        if (io_object_t ptr = storage) {
            IOObjectRetain(ptr);
        }
    }
    IOObjectPointer& operator=(const IOObjectPointer & other) {
        if (io_object_t pointer = storage) {
            IOObjectRelease(pointer);
        }
        storage = other.storage;
        if (io_object_t ptr = storage) {
            IOObjectRetain(ptr);
        }
        return *this;
    }

    IOObjectPointer(IOObjectPointer && other) : storage(std::exchange(other.storage, 0)) { }

    ~IOObjectPointer() {
        if (io_object_t pointer = storage) {
            IOObjectRelease(pointer);
        }
    }

    static inline IOObjectPointer<T> adopt(T ptr) {
        return IOObjectPointer<T>(ptr, IOObjectPointer<T>::Adopt);
    }

    T get() const {
        return fromStorageType(storage);
    }
    io_object_t* operator&()
    {
        if (io_object_t pointer = storage) {
            IOObjectRelease(pointer);
        }
        return &storage;
    }
    operator bool() const { return storage != 0; }

private:
    io_object_t storage;

    enum AdoptTag { Adopt };
    IOObjectPointer(T ptr, AdoptTag) : storage(toStorageType(ptr)) { }

    inline io_object_t toStorageType(io_object_t ptr) const {
        return (io_object_t)ptr;
    }

    inline T fromStorageType(io_object_t pointer) const {
        return (T)pointer;
    }

    void swap(IOObjectPointer &other) {
        std::swap(storage, other.storage);
    }
};

/// A smart pointer that can manage the lifecycle of IOKit plugin objects.
class IOCFPluginPointer {
public:
    IOCFPluginPointer() : _storage(nullptr) { }

    IOCFPluginPointer(const IOCFPluginPointer & other) = delete;

    IOCFPluginPointer(IOCFPluginPointer && other) : _storage(std::exchange(other._storage, nullptr)) { }

    ~IOCFPluginPointer() {
        if (IOCFPlugInInterface** pointer = _storage) {
            IODestroyPlugInInterface(pointer);
        }
    }

    IOCFPlugInInterface*** operator&()
    {
        if (IOCFPlugInInterface** pointer = _storage) {
            IODestroyPlugInInterface(pointer);
        }
        return &_storage;
    }

    HRESULT As(LPVOID* p, CFUUIDRef uuid) const
    {
        (*_storage)->AddRef(_storage); // Needed for some reason
        return (*_storage)->QueryInterface(_storage, CFUUIDGetUUIDBytes(uuid), p);
    }

    operator bool() const { return _storage != nullptr; }

    IOCFPlugInInterface** storage() const { return _storage; }

private:
    IOCFPlugInInterface** _storage;
    void swap(IOCFPluginPointer &other) {
        std::swap(_storage, other._storage);
    }
};

#endif // __IOKITPOINTER_HPP__
