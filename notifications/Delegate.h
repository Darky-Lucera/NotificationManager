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
#include <algorithm>
#include <atomic>
#include <functional>
//--
#include <cstdio>   // Replace fprintf by your logger

//-------------------------------------
namespace MindShake {

    // Version
    //---------------------------------
    static constexpr uint32_t kDelegateVersion = 2024'12'22;

    // Macros
    //---------------------------------
    #define kMethod(method)         void(Class::*method)(Args...)
    #define kEnableIfClassSizeT     template <typename Class> EnableIfClass<Class, size_t>
    #define kEnableIfClassDiffT     template <typename Class> EnableIfClass<Class, ptrdiff_t>
    #define kEnableIfClassBool      template <typename Class> EnableIfClass<Class, bool>

    // Utils
    //---------------------------------
    template <class Class>
    using Method = void (Class:: *)();

    template <class Class>
    using ConstMethod = void (Class:: *)() const;

    template <typename Class, typename... Args>
    using MethodArg = void (Class:: *)(Args...);

    template <typename Class, typename... Args>
    using ConstMethodArg = void (Class:: *)(Args...) const;

    template <typename Class, typename Type>
    using EnableIfClass = typename std::enable_if<std::is_class<Class>::value, Type>::type;

    //---------------------------------
    template <class Class>
    inline Method<Class>
    getNonConstMethod(Method<Class> method) {
        return method;
    }

    //---------------------------------
    template <class Class>
    inline ConstMethod<Class>
    getConstMethod(ConstMethod<Class> method) {
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
            class UnknownClass;

            using TFunc         = void (              *)(Args...);
            using TMethod       = void (UnknownClass::*)(Args...);  // Longest method signature

        protected:
            static size_t wrapperCounter;

            //-----------------------------
            struct Wrapper {
                                Wrapper() = default;
                explicit        Wrapper(TFunc f) : func(f) {}
                virtual         ~Wrapper() { ToBeRemoved(); }

                virtual void    operator()(const Args&... args) const {
                    if (object != nullptr)       ((object)->*(method))(args...);
                    else if (func != nullptr)    (*func)(args...);
                }

                void            ToBeRemoved() { object = nullptr, func = {}, isEnabled = false; }

                enum class Type { Unknown, Function, Method, Lambda, StdFunction };

                //--
                UnknownClass    *object {};
                union {
                    TMethod      method {};
                    TFunc        func;
                };
                size_t  id        = wrapperCounter++;
                Type    type      = Type::Unknown;
                bool    isEnabled = true;
            };

            //-----------------------------
            struct WrapperCFunc : Wrapper {
                explicit    WrapperCFunc(TFunc f) : Wrapper(f) { Wrapper::type = Wrapper::Type::Function; }

                void        operator()(const Args&... args) const override {
                    if (Wrapper::func != nullptr)
                        (*Wrapper::func)(args...);
                }
            };

            //-----------------------------
            struct WrapperMethod : Wrapper {
                        WrapperMethod() { Wrapper::type = Wrapper::Type::Method; }

                void    operator()(const Args&... args) const override {
                    if (Wrapper::object != nullptr)
                        ((Wrapper::object)->*(Wrapper::method))(args...);
                }
            };

            // Lambdas with captures
            //-----------------------------
            template <typename Lambda>
            struct WrapperLambda : Wrapper {
                explicit WrapperLambda(const Lambda &l) : lambda(l) { Wrapper::type = Wrapper::Type::Lambda; }

                void    operator()(const Args&... args) const override {
                    if (Wrapper::isEnabled)
                        lambda(args...);
                }

                Lambda  lambda;
            };

            //-----------------------------
            struct WrapperStdFunction : Wrapper {
                explicit WrapperStdFunction(const std::function<void(Args...)> &func) : function(func) { Wrapper::type = Wrapper::Type::StdFunction; }

                void    operator()(const Args&... args) const override {
                    if (Wrapper::isEnabled)
                        function(args...);
                }

                std::function<void(Args...)> function;
            };

        // Some helpers
        protected:
            template <class Class>
            static Class *
                                unConst(const Class *object)                          { return const_cast<Class *>(object);                          }

            template <typename Class>
            static MethodArg<Class, Args...>
                                unConst(void (Class:: *method)(Args...) const)        { return reinterpret_cast<MethodArg<Class, Args...>>(method);  }

        public:
                                Delegate() = default;
            virtual             ~Delegate()                                           { Clear();                                                     }

            //-------------------------
            // Add
            //-------------------------

            // avoid nullptr as lambda
            size_t              Add(std::nullptr_t)                                   { return size_t(-1);                                           }

            size_t              Add(TFunc func);

            kEnableIfClassSizeT Add(      Class *object)                              { return Add(object, getNonConstMethod(&Class::operator()));   }
            kEnableIfClassSizeT Add(const Class *object)                              { return Add(object, getConstMethod(&Class::operator()));      }
            kEnableIfClassSizeT Add(      Class *object, kMethod(method));
            kEnableIfClassSizeT Add(const Class *object, kMethod(method))             { return Add(unConst(object), method);                         }
            kEnableIfClassSizeT Add(      Class *object, kMethod(method) const)       { return Add(object, unConst(method));                         }
            kEnableIfClassSizeT Add(const Class *object, kMethod(method) const)       { return Add(unConst(object), unConst(method));                }
            // Mimic std::bind order
            kEnableIfClassSizeT Add(kMethod(method),             Class *object)       { return Add(object, method);                                  }
            kEnableIfClassSizeT Add(kMethod(method),       const Class *object)       { return Add(unConst(object), method);                         }
            kEnableIfClassSizeT Add(kMethod(method) const,       Class *object)       { return Add(object, unConst(method));                         }
            kEnableIfClassSizeT Add(kMethod(method) const, const Class *object)       { return Add(unConst(object), unConst(method));                }

            // Hack to detect lambdas with captures
            template <typename Lambda, typename std::enable_if<!std::is_assignable<Lambda, Lambda>::value, bool>::type = true>
            size_t              Add(const Lambda &lambda) {
                auto wrapper = new WrapperLambda<Lambda>(lambda);
                mWrappers.emplace_back(wrapper);
                return wrapper->id;
            }

            size_t Add(const std::function<void(Args...)> &func) {
                auto wrapper = new WrapperStdFunction(func);
                mWrappers.emplace_back(wrapper);
                return wrapper->id;
            }

            //-------------------------
            // Remove
            //-------------------------
            bool                Remove(std::nullptr_t)                                { return false;                                                }

            bool                Remove(TFunc func)                                    { return RemoveIndex(Find(func));                              }

            kEnableIfClassBool  Remove(      Class *object)                           { return RemoveIndex(Find(object, getNonConstMethod(&Class::operator())));       }
            kEnableIfClassBool  Remove(const Class *object)                           { return RemoveIndex(Find(unConst(object), getConstMethod(&Class::operator()))); }
            kEnableIfClassBool  Remove(      Class *object, kMethod(method))          { return RemoveIndex(Find(object, method));                    }
            kEnableIfClassBool  Remove(const Class *object, kMethod(method))          { return RemoveIndex(Find(unConst(object), method));           }
            kEnableIfClassBool  Remove(      Class *object, kMethod(method) const)    { return RemoveIndex(Find(object, unConst(method)));           }
            kEnableIfClassBool  Remove(const Class *object, kMethod(method) const)    { return RemoveIndex(Find(unConst(object), unConst(method)));  }
            // Mimic std::bind order
            kEnableIfClassBool  Remove(kMethod(method),             Class *object)    { return RemoveIndex(Find(object, method));                    }
            kEnableIfClassBool  Remove(kMethod(method),       const Class *object)    { return RemoveIndex(Find(unConst(object), method));           }
            kEnableIfClassBool  Remove(kMethod(method) const,       Class *object)    { return RemoveIndex(Find(object, unConst(method)));           }
            kEnableIfClassBool  Remove(kMethod(method) const, const Class *object)    { return RemoveIndex(Find(unConst(object), unConst(method)));  }
            // Hack to detect lambdas with captures (and return a value)
            //template <typename Lambda, typename std::enable_if<!std::is_assignable<Lambda, Lambda>::value, bool>::type = true>
            //bool            Remove(const Lambda &l) {
            //    static_assert(false, "You cannot remove a complex lambda");
            //    return -1;
            //}

            //-------------------------
            bool                RemoveById(size_t id);

            //-------------------------
            // operator()
            //-------------------------
            // The issue with perfect forwarding in this context is that we can not pass rValues to more than one function.
            // So, we need the other version of operator() to pass const references.
            // In any case, the Wrappers cannot have both operators() because they are virtual functions,
            // and a template function cannot be virtual.
            //-------------------------
            template <typename Dummy = void>
            typename std::enable_if<sizeof...(Args) != 0, Dummy>
            ::type              operator()(Args&&... args);

            void                operator()(const Args&... args);

            //-------------------------
            // Other
            //-------------------------
            size_t              GetNumDelegates() const                               { return mWrappers.size() - mToRemove.size();                  }

            void                Clear();

        protected:
            //-------------------------
            // Find
            //-------------------------

            ptrdiff_t           Find(std::nullptr_t)                                   { return -1;                                                   }

            ptrdiff_t           Find(const TFunc func) const;

            kEnableIfClassDiffT Find(Class *object) const                              { return Find(object, getNonConstMethod(&Class::operator()));  }
            kEnableIfClassDiffT Find(const Class *object) const                        { return Find(object, getConstMethod(&Class::operator()));     }
            kEnableIfClassDiffT Find(Class *object, kMethod(method)) const;
            kEnableIfClassDiffT Find(Class *object, kMethod(method) const) const       { return Find(object, unConst(method));                        }
            kEnableIfClassDiffT Find(const Class *object, kMethod(method)) const       { return Find(unConst(object), method);                        }
            kEnableIfClassDiffT Find(const Class *object, kMethod(method) const) const { return Find(unConst(object), unConst(method));               }
            kEnableIfClassDiffT Find(kMethod(method), Class *object) const             { return Find(object, method);                                 }
            kEnableIfClassDiffT Find(kMethod(method) const, Class *object) const       { return Find(object, unConst(method));                        }
            kEnableIfClassDiffT Find(kMethod(method), const Class *object) const       { return Find(unConst(object), method);                        }
            kEnableIfClassDiffT Find(kMethod(method) const, const Class *object) const { return Find(unConst(object), unConst(method));               }
            // Hack to detect lambdas with captures (and return a value)
            //template <typename Lambda, std::enable_if_t<!std::is_assignable_v<Lambda, Lambda>, bool> = true>
            //ptrdiff_t       Find(const Lambda &l) {
            //    static_assert(false, "You cannot find a complex lambda");
            //    return -1;
            //}

        protected:
            bool            RemoveIndex(ptrdiff_t idx);

            void            RemoveLazyDeleted();

        protected:
            std::vector<Wrapper *>  mWrappers;
            std::vector<size_t>     mToRemove;
            std::atomic_bool        mIsRunning {};
    };

    //---------------------------------

    //---------------------------------
    template <typename ...Args>
    size_t Delegate<void(Args...)>::wrapperCounter = 0;

    //---------------------------------
    // Add
    //---------------------------------
    template <typename ...Args>
    inline size_t
    Delegate<void(Args...)>::Add(TFunc func) {
        if (func != nullptr) {
            auto wrapper = new WrapperCFunc(func);
            mWrappers.emplace_back(wrapper);
            return wrapper->id;
        }

        return size_t(-1);
    }

    //---------------------------------
    template <typename ...Args>
    template <typename Class>
    inline typename std::enable_if<std::is_class<Class>::value, size_t>::type
    Delegate<void(Args...)>::Add(Class *object, void(Class::*method)(Args...)) {
        if (object == nullptr || method == nullptr)
            return size_t(-1);

        WrapperMethod   *wrapper = new WrapperMethod;

        wrapper->object = reinterpret_cast<UnknownClass *>(object);

    #if defined(_MSC_VER)
        memset(reinterpret_cast<void *>(&wrapper->method), 0, sizeof(TMethod));
        memcpy(reinterpret_cast<void *>(&wrapper->method), reinterpret_cast<void *>(&method), sizeof(method));
    #else
        wrapper->method = reinterpret_cast<TMethod>(method);
    #endif

        mWrappers.emplace_back(wrapper);

        return wrapper->id;
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
        if (idx >= 0) {
            if (mIsRunning == false) {
                delete mWrappers[idx];
                mWrappers.erase(mWrappers.begin() + idx);
            }
            else {
                mWrappers[idx]->ToBeRemoved();
                mToRemove.emplace_back(idx);
            }

            return true;
        }

        return false;
    }

    //---------------------------------
    template <typename ...Args>
    inline void
    Delegate<void(Args...)>::RemoveLazyDeleted() {
        ptrdiff_t   i, size;

        std::sort(mToRemove.begin(), mToRemove.end());
        size = mToRemove.size() - 1;
        for (i=size; i>=0; --i) {
            delete mWrappers[mToRemove[i]];
            mWrappers.erase(mWrappers.begin() + mToRemove[i]);
        }

        mToRemove.clear();
    }

    //---------------------------------
    // The issue with perfect forwarding in this context is that we can not pass rValues to more than one function.
    // So, we need the other version of operator() to pass const references.
    // In any case, the Wrappers cannot have both operators() because they are virtual functions,
    // and a template function cannot be virtual.
    //---------------------------------
    template <typename ...Args>
    template <typename Dummy>
    typename std::enable_if<sizeof...(Args) != 0, Dummy>::type
    Delegate<void(Args...)>::operator()(Args&&... args) {
        if (mIsRunning.exchange(true) == false) {
            // In this way we allow adding functions but not deleting them
            size_t size = mWrappers.size();
            for (size_t i = 0; i < size; ++i) {
                auto &wrapper = *mWrappers[i];
                try {
                    wrapper(std::forward<Args>(args)...);
                }
                catch (const std::exception &e) {
                    fprintf(stderr, "Exception calling user function %ud: %s", unsigned(wrapper.id), e.what());
                }
                catch (...) {
                    fprintf(stderr, "Unknown exception calling user function");
                }
            }
            RemoveLazyDeleted();
            mIsRunning = false;
        }
    }

    //---------------------------------
    template <typename ...Args>
    void
    Delegate<void(Args...)>::operator()(const Args&... args) {
        if (mIsRunning.exchange(true) == false) {
            // In this way we allow adding functions but not deleting them
            size_t size = mWrappers.size();
            for (size_t i = 0; i < size; ++i) {
                auto &wrapper = *mWrappers[i];
                try {
                    wrapper(args...);
                }
                catch (const std::exception &e) {
                    fprintf(stderr, "Exception calling user function %ud: %s", unsigned(wrapper.id), e.what());
                }
                catch (...) {
                    fprintf(stderr, "Unknown exception calling user function");
                }
            }
            RemoveLazyDeleted();
            mIsRunning = false;
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
            if (wrapper->func == func) {
                return i;
            }
            ++i;
        }

        return -1;
    }

    //---------------------------------
    template <typename ...Args>
    template <typename Class>
    inline typename std::enable_if<std::is_class<Class>::value, ptrdiff_t>::type
    Delegate<void(Args...)>::Find(Class *object, void(Class::*method)(Args...)) const {
        ptrdiff_t   i = 0;

        for (const auto *wrapper : mWrappers) {
            if (wrapper->object == reinterpret_cast<UnknownClass *>(object)) {
    #if defined(_MSC_VER)
                if (memcmp(&wrapper->method, reinterpret_cast<void *>(&method), sizeof(method)) == 0) {
    #else
                if (wrapper->method == reinterpret_cast<TMethod>(method)) {
    #endif
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
        for (auto *w : mWrappers) {
            delete w;
        }

        mWrappers.clear();
        mToRemove.clear();
    }

} // end of namespace

#undef kMethod
#undef kEnableIfClassSizeT
#undef kEnableIfClassPtrDiffT
#undef kEnableIfClassBool
