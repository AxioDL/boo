#ifndef __CFPOINTER_HPP__
#define __CFPOINTER_HPP__

#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <utility>

/// A smart pointer that can manage the lifecycle of Core Foundation objects.
template<typename T>
class CFPointer {
public:
    CFPointer() : storage(nullptr) { }

    CFPointer(T pointer) : storage(toStorageType(pointer)) {
        if (storage) {
            CFRetain(storage);
        }
    }

    CFPointer(const CFPointer & other) : storage(other.storage) {
        if (CFTypeRef ptr = storage) {
            CFRetain(ptr);
        }
    }

    CFPointer(CFPointer && other) : storage(std::exchange(other.storage, nullptr)) { }

    ~CFPointer() {
        if (CFTypeRef pointer = storage) {
            CFRelease(pointer);
        }
    }

    static inline CFPointer<T> adopt(T CF_RELEASES_ARGUMENT ptr);

    T get() const;
    CFPointer &operator=(CFPointer);
    CFTypeRef* operator&()
    {
        if (CFTypeRef pointer = storage) {
            CFRelease(pointer);
        }
        return &storage;
    }
    operator bool() const { return storage != nullptr; }

private:
    CFTypeRef storage;

    enum AdoptTag { Adopt };
    CFPointer(T ptr, AdoptTag) : storage(toStorageType(ptr)) { }

    inline CFTypeRef toStorageType(CFTypeRef ptr) const {
        return (CFTypeRef)ptr;
    }

    inline T fromStorageType(CFTypeRef pointer) const {
        return (T)pointer;
    }

    void swap(CFPointer &);
};

template<typename T>
CFPointer<T> CFPointer<T>::adopt(T CF_RELEASES_ARGUMENT ptr) {
    return CFPointer<T>(ptr, CFPointer<T>::Adopt);
}

template<typename T>
T CFPointer<T>::get() const {
    return fromStorageType(storage);
}

template<typename T>
inline CFPointer<T>& CFPointer<T>::operator=(CFPointer other) {
    swap(other);
    return *this;
}

template<typename T>
inline void CFPointer<T>::swap(CFPointer &other) {
    std::swap(storage, other.storage);
}

/// A smart pointer that can manage the lifecycle of CoreFoundation IUnknown objects.
template<typename T>
class IUnknownPointer {
public:
    IUnknownPointer() : _storage(nullptr) { }

    IUnknownPointer(T** pointer) : _storage(toStorageType(pointer)) {
        if (_storage) {
            (*pointer)->AddRef(pointer);
        }
    }

    IUnknownPointer(const IUnknownPointer & other) : _storage(other._storage) {
        if (IUnknownVTbl** ptr = _storage) {
            (*ptr)->AddRef(ptr);
        }
    }
    IUnknownPointer& operator=(const IUnknownPointer & other) {
        if (IUnknownVTbl** pointer = _storage) {
            (*pointer)->Release(pointer);
        }
        _storage = other._storage;
        if (IUnknownVTbl** ptr = _storage) {
            (*ptr)->AddRef(ptr);
        }
        return *this;
    }

    IUnknownPointer(IUnknownPointer && other) : _storage(std::exchange(other._storage, nullptr)) { }

    ~IUnknownPointer() {
        if (IUnknownVTbl** pointer = _storage) {
            (*pointer)->Release(pointer);
        }
    }

    static inline IUnknownPointer<T> adopt(T** ptr);

    T* get() const;
    T* operator->() const { return get(); }
    T** storage() const { return (T**)_storage; }
    LPVOID* operator&()
    {
        if (IUnknownVTbl** pointer = _storage) {
            printf("%p RELEASE %d\n", pointer, (*pointer)->Release(pointer));
        }
        return (LPVOID*)&_storage;
    }
    operator bool() const { return _storage != nullptr; }

private:
    IUnknownVTbl** _storage;

    enum AdoptTag { Adopt };
    IUnknownPointer(T** ptr, AdoptTag) : _storage(toStorageType(ptr)) { }

    inline IUnknownVTbl** toStorageType(T** ptr) const {
        return (IUnknownVTbl**)ptr;
    }

    inline T* fromStorageType(IUnknownVTbl** pointer) const {
        return *(T**)pointer;
    }

    void swap(IUnknownPointer &);
};

template<typename T>
IUnknownPointer<T> IUnknownPointer<T>::adopt(T** ptr) {
    return IUnknownPointer<T>(ptr, IUnknownPointer<T>::Adopt);
}

template<typename T>
T* IUnknownPointer<T>::get() const {
    return fromStorageType(_storage);
}

template<typename T>
inline void IUnknownPointer<T>::swap(IUnknownPointer &other) {
    std::swap(_storage, other._storage);
}

#endif // __CFPOINTER_HPP__
