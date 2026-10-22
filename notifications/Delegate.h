#pragma once

//-----------------------------------------------------------------------------
// Copyright (C) 2006-present Carlos Aragonés
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt
//-----------------------------------------------------------------------------
// Last version here: https://github.com/Darky-Lucera/delegate
//-----------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>
#include <vector>
#include <type_traits>
#include <atomic>
#include <functional>
#include <memory>
#include <utility>
//--
// Define DELEGATE_LOG_ERROR(format, ...) before including this file to use your own logger
#ifndef DELEGATE_LOG_ERROR
    #include <cstdio>
    #define DELEGATE_LOG_ERROR(...)     fprintf(stderr, __VA_ARGS__)
#endif

//-------------------------------------
namespace MindShake {

    // Version
    //---------------------------------
    static constexpr uint32_t kDelegateVersion = 20261022;

    // Macros
    //---------------------------------
    #define kMethod(method)         void(Class::*method)(Args...)
    #define kEnableIfClassSizeT     template <typename Class> EnableIfClass<Class, size_t>
    #define kEnableIfClassDiffT     template <typename Class> EnableIfClass<Class, ptrdiff_t>
    #define kEnableIfClassBool      template <typename Class> EnableIfClass<Class, bool>
    #define kEnableIfBaseSizeT      template <typename Object, typename Class> EnableIfBase<Class, Object, size_t>
    #define kEnableIfBaseDiffT      template <typename Object, typename Class> EnableIfBase<Class, Object, ptrdiff_t>
    #define kEnableIfBaseBool       template <typename Object, typename Class> EnableIfBase<Class, Object, bool>

    // Utils
    //---------------------------------
    template <typename Class, typename... Args>
    using MethodArg = void (Class:: *)(Args...);

    template <typename Class, typename... Args>
    using ConstMethodArg = void (Class:: *)(Args...) const;

    template <typename Class, typename Type>
    using EnableIfClass = typename std::enable_if<std::is_class<Class>::value, Type>::type;

    // Object can be Class or derive from it, so methods inherited from a base class can be used
    template <typename Class, typename Object, typename Type>
    using EnableIfBase = typename std::enable_if<std::is_base_of<Class, Object>::value && !std::is_const<Object>::value, Type>::type;

    // Pick the const or the non-const version of an overloaded method.
    // If there are several overloads with the same constness, give the parameters: GetConstMethod<MyClass, int>(&MyClass::method)
    //---------------------------------
    template <typename Class, typename... Args>
    inline MethodArg<Class, Args...>
    GetNonConstMethod(MethodArg<Class, Args...> method) {
        return method;
    }

    //---------------------------------
    template <typename Class, typename... Args>
    inline ConstMethodArg<Class, Args...>
    GetConstMethod(ConstMethodArg<Class, Args...> method) {
        return method;
    }

    //---------------------------------

    //---------------------------------
    template <typename T>
    class Delegate;

    //---------------------------------
    template <typename ...Args>
    class Delegate<void(Args...)> {
        public:
            using TFunc = void (*)(Args...);

        protected:
            // One tag per type, to know which wrapper type a Wrapper * really is without RTTI.
            // Writable on purpose: the linker may merge identical read-only data (MSVC /OPT:ICF),
            // and then two types would share the same tag.
            template <typename T>
            static const void * TypeTag() { static char tag = 0; return &tag; }

            //-----------------------------
            struct Wrapper {
                             Wrapper()  = default;
                virtual      ~Wrapper() = default;

                virtual void operator()(const Args&... args) const = 0;

                // Function and method wrappers also clear their pointer, so their operator() tests a value
                // it has to load anyway. Reading isEnabled there as well made every call measurably slower.
                virtual void ToBeRemoved() { isEnabled = false; }

                enum class Type {
                    Unknown,
                    Function,
                    Method,
                    Lambda,
                    StdFunction
                };

                //--
                size_t      id        = 0;
                const void  *tag      = nullptr;
                Type        type      = Type::Unknown;
                bool        isEnabled = true;
            };

            //-----------------------------
            struct WrapperCFunc : Wrapper {
                explicit WrapperCFunc(TFunc f) : func(f) { Wrapper::type = Wrapper::Type::Function; }

                void     operator()(const Args&... args) const override {
                    if (func != nullptr) {
                        (*func)(args...);
                    }
                }

                void     ToBeRemoved() override { Wrapper::ToBeRemoved(); func = nullptr; }

                TFunc   func;
            };

            // MethodPtr is void (Class::*)(Args...) or void (Class::*)(Args...) const.
            // Keeping the real type lets the compiler use the right member pointer layout for Class
            // (MSVC uses different sizes for single, multiple and virtual inheritance).
            //-----------------------------
            template <typename Class, typename MethodPtr>
            struct WrapperMethod : Wrapper {
                        WrapperMethod(Class *o, MethodPtr m) : object(o), method(m) {
                            Wrapper::type = Wrapper::Type::Method;
                            Wrapper::tag  = TypeTag<WrapperMethod>();
                        }

                void    operator()(const Args&... args) const override {
                    if (object != nullptr) {
                        (object->*method)(args...);
                    }
                }

                void    ToBeRemoved() override { Wrapper::ToBeRemoved(); object = nullptr; }

                Class       *object;
                MethodPtr   method;
            };

            // Lambdas with captures
            //-----------------------------
            template <typename Lambda>
            struct WrapperLambda : Wrapper {
                explicit WrapperLambda(const Lambda &l) : lambda(l) { Wrapper::type = Wrapper::Type::Lambda; }

                void     operator()(const Args&... args) const override {
                    if (Wrapper::isEnabled) {
                        lambda(args...);
                    }
                }

                mutable Lambda lambda;
            };

            //-----------------------------
            struct WrapperStdFunction : Wrapper {
                explicit WrapperStdFunction(const std::function<void(Args...)> &func) : function(func) { Wrapper::type = Wrapper::Type::StdFunction; }

                void     operator()(const Args&... args) const override {
                    if (Wrapper::isEnabled) {
                        function(args...);
                    }
                }

                std::function<void(Args...)> function;
            };

        // Some helpers
        protected:
            template <class Class>
            static Class *
                                UnConst(const Class *object)                          { return const_cast<Class *>(object);                          }

            // The wrapper ignores what operator() returns, so a functor can return any type
            template <typename Class>
            using CallOperatorPtr      = decltype(std::declval<Class &>()(std::declval<Args>()...)) (Class::*)(Args...);

            template <typename Class>
            using ConstCallOperatorPtr = decltype(std::declval<const Class &>()(std::declval<Args>()...)) (Class::*)(Args...) const;

            template <typename Class>
            static ConstCallOperatorPtr<Class>
                                GetConstCallOperator()                                { return &Class::operator();                                   }

            // Same choice as a call on a non-const object: the non-const operator() if it exists, else the const one.
            template <typename Class>
            static auto         GetCallOperator(int) -> decltype(static_cast<CallOperatorPtr<Class>>(&Class::operator()))
                                                                                      { return &Class::operator();                                   }

            template <typename Class>
            static ConstCallOperatorPtr<Class>
                                GetCallOperator(long)                                 { return GetConstCallOperator<Class>();                        }

        public:
                                Delegate() = default;
                                Delegate(Delegate &&other) noexcept                   { *this = std::move(other);                                    }
            virtual             ~Delegate()                                           { Clear();                                                     }

            Delegate &          operator=(Delegate &&other) noexcept;

            //-------------------------
            // Add
            //-------------------------

            // avoid nullptr as lambda
            size_t              Add(std::nullptr_t)                                   { return size_t(-1);                                           }

            size_t              Add(TFunc func);

            kEnableIfClassSizeT Add(      Class *object)                              { return AddMethod(object, GetCallOperator<Class>(0));                  }
            kEnableIfClassSizeT Add(const Class *object)                              { return AddMethod(UnConst(object), GetConstCallOperator<Class>());     }
            kEnableIfBaseSizeT  Add(      Object *object, kMethod(method))            { return AddMethod(static_cast<Class *>(object), method);                }
            kEnableIfBaseSizeT  Add(const Object *object, kMethod(method))            { return AddMethod(UnConst(static_cast<const Class *>(object)), method); }
            kEnableIfBaseSizeT  Add(      Object *object, kMethod(method) const)      { return AddMethod(static_cast<Class *>(object), method);                }
            kEnableIfBaseSizeT  Add(const Object *object, kMethod(method) const)      { return AddMethod(UnConst(static_cast<const Class *>(object)), method); }
            // Mimic std::bind order
            kEnableIfBaseSizeT  Add(kMethod(method),             Object *object)      { return Add(object, method);                                  }
            kEnableIfBaseSizeT  Add(kMethod(method),       const Object *object)      { return Add(object, method);                                  }
            kEnableIfBaseSizeT  Add(kMethod(method) const,       Object *object)      { return Add(object, method);                                  }
            kEnableIfBaseSizeT  Add(kMethod(method) const, const Object *object)      { return Add(object, method);                                  }
            // Methods with & or const & qualifiers (&& is not supported: it would be called again and again on the same object)
            kEnableIfBaseSizeT  Add(      Object *object, kMethod(method) &)          { return AddMethod(static_cast<Class *>(object), method);                }
            kEnableIfBaseSizeT  Add(const Object *object, kMethod(method) &)          { return AddMethod(UnConst(static_cast<const Class *>(object)), method); }
            kEnableIfBaseSizeT  Add(      Object *object, kMethod(method) const &)    { return AddMethod(static_cast<Class *>(object), method);                }
            kEnableIfBaseSizeT  Add(const Object *object, kMethod(method) const &)    { return AddMethod(UnConst(static_cast<const Class *>(object)), method); }
            kEnableIfBaseSizeT  Add(kMethod(method) &,             Object *object)    { return Add(object, method);                                  }
            kEnableIfBaseSizeT  Add(kMethod(method) &,       const Object *object)    { return Add(object, method);                                  }
            kEnableIfBaseSizeT  Add(kMethod(method) const &,       Object *object)    { return Add(object, method);                                  }
            kEnableIfBaseSizeT  Add(kMethod(method) const &, const Object *object)    { return Add(object, method);                                  }

            // Lambdas without captures are stored as plain functions, so Remove(lambda) can find them
            template <typename Lambda, typename std::enable_if<std::is_class<Lambda>::value && std::is_convertible<Lambda, TFunc>::value, bool>::type = true>
            size_t              Add(const Lambda &lambda)                             { return Add(static_cast<TFunc>(lambda));                      }

            // Hack to detect lambdas with captures
            template <typename Lambda, typename std::enable_if<!std::is_assignable<Lambda, Lambda>::value && !std::is_convertible<Lambda, TFunc>::value, bool>::type = true>
            size_t              Add(const Lambda &lambda)                             { return AddWrapper(new WrapperLambda<Lambda>(lambda));        }

            size_t              Add(const std::function<void(Args...)> &func)         { return func ? AddWrapper(new WrapperStdFunction(func)) : size_t(-1); }

            //-------------------------
            // Remove
            //-------------------------
            bool                Remove(std::nullptr_t)                                { return false;                                                }

            bool                Remove(TFunc func)                                    { return RemoveIndex(Find(func));                              }

            kEnableIfClassBool  Remove(      Class *object)                           { return RemoveIndex(FindMethod(object, GetCallOperator<Class>(0)));              }
            kEnableIfClassBool  Remove(const Class *object)                           { return RemoveIndex(FindMethod(UnConst(object), GetConstCallOperator<Class>())); }
            kEnableIfBaseBool   Remove(      Object *object, kMethod(method))         { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(const Object *object, kMethod(method))         { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(      Object *object, kMethod(method) const)   { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(const Object *object, kMethod(method) const)   { return RemoveIndex(Find(object, method));                    }
            // Mimic std::bind order
            kEnableIfBaseBool   Remove(kMethod(method),             Object *object)   { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(kMethod(method),       const Object *object)   { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(kMethod(method) const,       Object *object)   { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(kMethod(method) const, const Object *object)   { return RemoveIndex(Find(object, method));                    }
            // Methods with & or const & qualifiers
            kEnableIfBaseBool   Remove(      Object *object, kMethod(method) &)       { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(const Object *object, kMethod(method) &)       { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(      Object *object, kMethod(method) const &) { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(const Object *object, kMethod(method) const &) { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(kMethod(method) &,             Object *object) { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(kMethod(method) &,       const Object *object) { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(kMethod(method) const &,       Object *object) { return RemoveIndex(Find(object, method));                    }
            kEnableIfBaseBool   Remove(kMethod(method) const &, const Object *object) { return RemoveIndex(Find(object, method));                    }

            //-------------------------
            bool                RemoveById(size_t id);

            //-------------------------
            // operator()
            //-------------------------
            // A call to operator() while it is already running (from a callback or another thread) is ignored,
            // and returns false.
            //
            // Only the non-const version frees the functions removed during the call.
            //-------------------------
            bool                operator()(const Args&... args);
            bool                operator()(const Args&... args) const;

            //-------------------------
            // Other
            //-------------------------
            size_t              GetNumDelegates() const                                 { return mWrappers.size() - mNumToRemove;                      }

            bool                IsRunning() const                                       { return mIsRunning.load(std::memory_order_relaxed);           }

            void                Clear();

        protected:
            //-------------------------
            // Find
            //-------------------------

            ptrdiff_t           Find(std::nullptr_t) const                              { return -1;                                                   }

            ptrdiff_t           Find(const TFunc func) const;

            kEnableIfClassDiffT Find(Class *object) const                               { return FindMethod(object, GetCallOperator<Class>(0));                  }
            kEnableIfClassDiffT Find(const Class *object) const                         { return FindMethod(UnConst(object), GetConstCallOperator<Class>());     }
            kEnableIfBaseDiffT  Find(Object *object, kMethod(method)) const             { return FindMethod(static_cast<Class *>(object), method);                }
            kEnableIfBaseDiffT  Find(Object *object, kMethod(method) const) const       { return FindMethod(static_cast<Class *>(object), method);                }
            kEnableIfBaseDiffT  Find(const Object *object, kMethod(method)) const       { return FindMethod(UnConst(static_cast<const Class *>(object)), method); }
            kEnableIfBaseDiffT  Find(const Object *object, kMethod(method) const) const { return FindMethod(UnConst(static_cast<const Class *>(object)), method); }
            kEnableIfBaseDiffT  Find(kMethod(method), Object *object) const             { return Find(object, method);                                 }
            kEnableIfBaseDiffT  Find(kMethod(method) const, Object *object) const       { return Find(object, method);                                 }
            kEnableIfBaseDiffT  Find(kMethod(method), const Object *object) const       { return Find(object, method);                                 }
            kEnableIfBaseDiffT  Find(kMethod(method) const, const Object *object) const { return Find(object, method);                                 }
            // Methods with & or const & qualifiers
            kEnableIfBaseDiffT  Find(Object *object, kMethod(method) &) const             { return FindMethod(static_cast<Class *>(object), method);                }
            kEnableIfBaseDiffT  Find(Object *object, kMethod(method) const &) const       { return FindMethod(static_cast<Class *>(object), method);                }
            kEnableIfBaseDiffT  Find(const Object *object, kMethod(method) &) const       { return FindMethod(UnConst(static_cast<const Class *>(object)), method); }
            kEnableIfBaseDiffT  Find(const Object *object, kMethod(method) const &) const { return FindMethod(UnConst(static_cast<const Class *>(object)), method); }
            kEnableIfBaseDiffT  Find(kMethod(method) &, Object *object) const             { return Find(object, method);                                 }
            kEnableIfBaseDiffT  Find(kMethod(method) const &, Object *object) const       { return Find(object, method);                                 }
            kEnableIfBaseDiffT  Find(kMethod(method) &, const Object *object) const       { return Find(object, method);                                 }
            kEnableIfBaseDiffT  Find(kMethod(method) const &, const Object *object) const { return Find(object, method);                                 }

        protected:
            size_t          AddWrapper(Wrapper *wrapper);

            // The object is always stored as non-const, so Add and Remove match whatever the
            // constness of the pointer they receive.
            template <typename Class, typename MethodPtr>
            size_t          AddMethod(Class *object, MethodPtr method);

            template <typename Class, typename MethodPtr>
            ptrdiff_t       FindMethod(Class *object, MethodPtr method) const;

            bool            RemoveIndex(ptrdiff_t idx);

            void            RemoveLazyDeleted();

            void            CallAll(const Args&... args) const;

        protected:
            std::vector<Wrapper *>      mWrappers;
            size_t                      mNumToRemove {};
            size_t                      mNextId {};
            mutable std::atomic_bool    mIsRunning {};
    };

    //---------------------------------

    //---------------------------------
    template <typename ...Args>
    inline Delegate<void(Args...)> &
    Delegate<void(Args...)>::operator=(Delegate &&other) noexcept {
        if (this != &other) {
            Clear();
            mWrappers.swap(other.mWrappers);
            std::swap(mNumToRemove, other.mNumToRemove);
            std::swap(mNextId, other.mNextId);
        }

        return *this;
    }

    //---------------------------------
    // Add
    //---------------------------------
    template <typename ...Args>
    inline size_t
    Delegate<void(Args...)>::Add(TFunc func) {
        if (func != nullptr) {
            return AddWrapper(new WrapperCFunc(func));
        }

        return size_t(-1);
    }

    //---------------------------------
    template <typename ...Args>
    inline size_t
    Delegate<void(Args...)>::AddWrapper(Wrapper *wrapper) {
        // emplace_back can throw when the vector grows, and then nobody else would delete the wrapper
        std::unique_ptr<Wrapper> owner(wrapper);
        // Each delegate has its own counter, so the ids stay unique even if several modules (DLLs) add to it
        wrapper->id = mNextId++;
        mWrappers.emplace_back(wrapper);
        owner.release();

        return wrapper->id;
    }

    //---------------------------------
    template <typename ...Args>
    template <typename Class, typename MethodPtr>
    inline size_t
    Delegate<void(Args...)>::AddMethod(Class *object, MethodPtr method) {
        if (object == nullptr || method == nullptr) {
            return size_t(-1);
        }

        return AddWrapper(new WrapperMethod<Class, MethodPtr>(object, method));
    }

    //---------------------------------
    // Remove
    //---------------------------------
    template <typename ...Args>
    inline bool
    Delegate<void(Args...)>::RemoveById(size_t id) {
        for (size_t i=0; i<mWrappers.size(); ++i) {
            if (mWrappers[i]->id == id) {
                return RemoveIndex(i);
            }
        }

        return false;
    }

    //---------------------------------
    template <typename ...Args>
    inline bool
    Delegate<void(Args...)>::RemoveIndex(ptrdiff_t idx) {
        if (idx >= 0 && mWrappers[idx]->isEnabled) {
            if (mIsRunning == false) {
                delete mWrappers[idx];
                mWrappers.erase(mWrappers.begin() + idx);
            }
            else {
                mWrappers[idx]->ToBeRemoved();
                ++mNumToRemove;
            }

            return true;
        }

        return false;
    }

    //---------------------------------
    template <typename ...Args>
    inline void
    Delegate<void(Args...)>::RemoveLazyDeleted() {
        // The wrappers waiting to be removed are the ones that ToBeRemoved() disabled.
        // Move them to the end, keeping the order of the others.
        size_t  count = 0;

        for (size_t i = 0; i < mWrappers.size(); ++i) {
            if (mWrappers[i]->isEnabled) {
                std::swap(mWrappers[count++], mWrappers[i]);
            }
        }

        // Deleting a lambda destroys what it captured, and that code may use this delegate again.
        // Taking them out one by one from the end keeps the vector valid at every moment, and the
        // isEnabled check stops at a function added by that code.
        while (mWrappers.size() > count && mWrappers.back()->isEnabled == false) {
            Wrapper *wrapper = mWrappers.back();
            mWrappers.pop_back();
            --mNumToRemove;
            delete wrapper;
        }
    }

    //---------------------------------
    template <typename ...Args>
    bool
    Delegate<void(Args...)>::operator()(const Args&... args) {
        // acquire/release is the ordering a lock needs. The default seq_cst store would be
        // a second locked instruction on x86, which made every call measurably slower.
        if (mIsRunning.exchange(true, std::memory_order_acquire) == false) {
            CallAll(args...);

            if (mNumToRemove != 0) {
                RemoveLazyDeleted();
            }

            mIsRunning.store(false, std::memory_order_release);

            return true;
        }

        return false;
    }

    //---------------------------------
    template <typename ...Args>
    bool
    Delegate<void(Args...)>::operator()(const Args&... args) const {
        if (mIsRunning.exchange(true, std::memory_order_acquire) == false) {
            CallAll(args...);
            mIsRunning.store(false, std::memory_order_release);

            return true;
        }

        return false;
    }

    //---------------------------------
    template <typename ...Args>
    inline void
    Delegate<void(Args...)>::CallAll(const Args&... args) const {
        // In this way we allow adding functions but not deleting them
        size_t size = mWrappers.size();
        for (size_t i = 0; i < size; ++i) {
            auto &wrapper = *mWrappers[i];
            try {
                wrapper(args...);
            }
            catch (const std::exception &e) {
                (void) e;   // DELEGATE_LOG_ERROR may be defined to ignore its arguments
                DELEGATE_LOG_ERROR("Exception calling user function %zu: %s\n", wrapper.id, e.what());
            }
            catch (...) {
                DELEGATE_LOG_ERROR("Unknown exception calling user function %zu\n", wrapper.id);
            }
        }
    }

    //---------------------------------
    // Find
    //---------------------------------
    template <typename ...Args>
    inline ptrdiff_t
    Delegate<void(Args...)>::Find(const TFunc func) const {
        ptrdiff_t   i = 0;

        for (const auto *wrapper : mWrappers) {
            if (wrapper->isEnabled && wrapper->type == Wrapper::Type::Function) {
                if (static_cast<const WrapperCFunc *>(wrapper)->func == func) {
                    return i;
                }
            }
            ++i;
        }

        return -1;
    }

    //---------------------------------
    template <typename ...Args>
    template <typename Class, typename MethodPtr>
    inline ptrdiff_t
    Delegate<void(Args...)>::FindMethod(Class *object, MethodPtr method) const {
        using WrapperType = WrapperMethod<Class, MethodPtr>;

        const void  *tag = TypeTag<WrapperType>();
        ptrdiff_t   i    = 0;

        for (const auto *wrapper : mWrappers) {
            if (wrapper->isEnabled && wrapper->tag == tag) {
                const auto *wrapperMethod = static_cast<const WrapperType *>(wrapper);
                if (wrapperMethod->object == object && wrapperMethod->method == method) {
                    return i;
                }
            }
            ++i;
        }

        return -1;
    }

    //---------------------------------
    template <typename ...Args>
    inline void
    Delegate<void(Args...)>::Clear() {
        // A callback can call Clear(), and its own wrapper must stay alive until it returns
        if (mIsRunning) {
            for (size_t i = 0; i < mWrappers.size(); ++i) {
                RemoveIndex(i);
            }

            return;
        }

        // Deleting a lambda destroys what it captured, and that code may use this delegate again.
        // Taking the wrappers out first means it never sees one that is already freed.
        std::vector<Wrapper *> wrappers;
        wrappers.swap(mWrappers);
        mNumToRemove = 0;

        for (auto *w : wrappers) {
            delete w;
        }
    }

} // end of namespace

#undef kMethod
#undef kEnableIfClassSizeT
#undef kEnableIfClassDiffT
#undef kEnableIfClassBool
#undef kEnableIfBaseSizeT
#undef kEnableIfBaseDiffT
#undef kEnableIfBaseBool
