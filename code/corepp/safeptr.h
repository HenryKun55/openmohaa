/*
===========================================================================
Copyright (C) 2025 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// safeptr.h: Safe Pointers

#pragma once

#include "Linklist.h"

#include <utility>

class SafePtrBase
{
private:
    void AddReference(Class *ptr);
    void RemoveReference(Class *ptr);

protected:
    SafePtrBase *prev;
    SafePtrBase *next;
    Class       *ptr;

public:
    SafePtrBase();
#ifdef __SWITCH__
    // Non-virtual on Switch. In the single-binary symbol-isolated build the
    // weak `vtable for SafePtr<T>` ends up with NULL destructor slots (the
    // COMDAT destructor gets discarded during ld -r while the vtable's reloc to
    // it survives), so `delete SafePtr<T>*` calls a virtual dtor through a NULL
    // slot -> PC=0. SafePtr is never deleted polymorphically via SafePtrBase*,
    // so the virtual is unnecessary; removing it drops the vtable entirely and
    // makes every `delete` a direct (static) call. Engine + game both compile
    // this header on Switch, so the layout stays consistent across the binary.
    ~SafePtrBase();
#else
    virtual ~SafePtrBase();
#endif
    void   InitSafePtr(Class *newptr);
    Class *Pointer(void);
    void   Clear(void);

protected:
    void Move(SafePtrBase&& other);
};

inline void SafePtrBase::AddReference(Class *ptr)
{
    if (!ptr->SafePtrList) {
        ptr->SafePtrList = this;
        LL_Reset(this, next, prev);
    } else {
        LL_Add(ptr->SafePtrList, this, next, prev);
    }
}

inline void SafePtrBase::RemoveReference(Class *ptr)
{
#ifdef __SWITCH__
    /* Defensive unlink: on aarch64 the plain LL_Remove faulted while
     * relinking a multi-node safe-pointer list during entity spawn / script
     * execution. Guard every dereference and only touch the head when it is
     * actually this node. Functionally identical when the list is well-formed,
     * but it won't dereference a stray NULL. */
    if (ptr->SafePtrList == this) {
        ptr->SafePtrList = (next == this) ? nullptr : next;
    }
    if (prev) {
        prev->next = next;
    }
    if (next) {
        next->prev = prev;
    }
    next = this;
    prev = this;
#else
    if (ptr->SafePtrList == this) {
        if (ptr->SafePtrList->next == this) {
            ptr->SafePtrList = nullptr;
        } else {
            ptr->SafePtrList = next;
            LL_Remove(this, next, prev);
        }
    } else {
        LL_Remove(this, next, prev);
    }
#endif
}

inline void SafePtrBase::Clear(void)
{
    if (ptr) {
        RemoveReference(ptr);
        ptr = nullptr;
    }
}

inline SafePtrBase::SafePtrBase()
{
    prev = nullptr;
    next = nullptr;
    ptr  = nullptr;
}

inline SafePtrBase::~SafePtrBase()
{
    Clear();
}

inline Class *SafePtrBase::Pointer(void)
{
    return ptr;
}

inline void SafePtrBase::InitSafePtr(Class *newptr)
{
    if (ptr != newptr) {
        if (ptr) {
            RemoveReference(ptr);
        }

        ptr = newptr;
        if (ptr == nullptr) {
            return;
        }

        AddReference(ptr);
    }
}

inline void SafePtrBase::Move(SafePtrBase&& other)
{
    prev = other.prev;
    next = other.next;
    ptr  = other.ptr;

    if (ptr && ptr->SafePtrList) {
        if (ptr->SafePtrList == &other) {
            ptr->SafePtrList = this;
        } else {
            if (ptr->SafePtrList->prev == &other) {
                ptr->SafePtrList->prev = this;
            }
            if (ptr->SafePtrList->next == &other) {
                ptr->SafePtrList->next = this;
            }
        }
    }

    if (prev) {
        if (prev == &other) {
            prev = this;
        } else {
            assert(prev->next == &other || prev->next == this);
            prev->next = this;
        }
    }
    if (next) {
        if (next == &other) {
            next = this;
        } else {
            assert(next->prev == &other || next->prev == this);
            next->prev = this;
        }
    }

    other.next = other.prev = NULL;
    other.ptr               = NULL;
}

template<class T>
class SafePtr : public SafePtrBase
{
public:
    SafePtr(T *objptr = 0);
    SafePtr(const SafePtr& obj);
    SafePtr(SafePtr&& obj);

    SafePtr& operator=(const SafePtr& obj);
    SafePtr& operator=(T *const obj);
    SafePtr& operator=(SafePtr&& obj);

#ifdef LINUX
    friend bool operator== <>(SafePtr<T> a, T *b);
    friend bool operator!= <>(SafePtr<T> a, T *b);
    friend bool operator== <>(T *a, SafePtr<T> b);
    friend bool operator!= <>(T *a, SafePtr<T> b);
    friend bool operator== <>(SafePtr<T> a, SafePtr<T> b);
    friend bool operator!= <>(SafePtr<T> a, SafePtr<T> b);
#else
    // The compiler/linker gets confused when the friend functions definition are not templated
    template<class U>
    friend bool operator==(SafePtr<U> a, U *b);
    template<class U>
    friend bool operator!=(SafePtr<U> a, U *b);
    template<class U>
    friend bool operator==(U *a, SafePtr<U> b);
    template<class U>
    friend bool operator!=(U *a, SafePtr<U> b);
    template<class U>
    friend bool operator==(SafePtr<U> a, SafePtr<U> b);
    template<class U>
    friend bool operator!=(SafePtr<U> a, SafePtr<U> b);
#endif

    bool operator!() const;
    operator T *() const;
    T *operator->() const;
    T& operator*() const;
};

template<class T>
inline SafePtr<T>::SafePtr(T *objptr)
{
    InitSafePtr((Class *)objptr);
}

template<class T>
inline SafePtr<T>::SafePtr(const SafePtr& obj)
{
    InitSafePtr(obj.ptr);
}

template<class T>
inline SafePtr<T>::SafePtr(SafePtr&& obj)
{
    Move(std::move(obj));
}

template<class T>
inline SafePtr<T>& SafePtr<T>::operator=(const SafePtr& obj)
{
    InitSafePtr(obj.ptr);
    return *this;
}

template<class T>
inline SafePtr<T>& SafePtr<T>::operator=(SafePtr&& obj)
{
    Clear();
    Move(std::move(obj));

    return *this;
}

template<class T>
inline SafePtr<T>& SafePtr<T>::operator=(T *const obj)
{
    InitSafePtr(obj);
    return *this;
}

template<class T>
inline bool operator==(SafePtr<T> a, T *b)
{
    return a.ptr == b;
}

template<class T>
inline bool operator!=(SafePtr<T> a, T *b)
{
    return a.ptr != b;
}

template<class T>
inline bool operator==(T *a, SafePtr<T> b)
{
    return a == b.ptr;
}

template<class T>
inline bool operator!=(T *a, SafePtr<T> b)
{
    return a != b.ptr;
}

template<class T>
inline bool operator==(SafePtr<T> a, SafePtr<T> b)
{
    return a.ptr == b.ptr;
}

template<class T>
inline bool operator!=(SafePtr<T> a, SafePtr<T> b)
{
    return a.ptr != b.ptr;
}

template<class T>
inline bool SafePtr<T>::operator!() const
{
    return ptr == nullptr;
}

template<class T>
inline SafePtr<T>::operator T *() const
{
    return (T *)ptr;
}

template<class T>
inline T *SafePtr<T>::operator->() const
{
    return (T *)ptr;
}

template<class T>
inline T& SafePtr<T>::operator*() const
{
    return *(T *)ptr;
}
