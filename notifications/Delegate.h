#pragma once

//-----------------------------------------------------------------------------
// Copyright (C) 2006-present Carlos Aragonés
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt
//-----------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>
#include <vector>
#include <type_traits>
#include <algorithm>

//-------------------------------------
namespace MindShake {

    #define kUnConst(method)        reinterpret_cast<void(Class::*)(Args...)>(method)
    #define kMethod(method)         void(Class::*method)(Args...)
    #define kOnlyClassSizeT         typename std::enable_if<std::is_class<Class>::value, size_t>::type
    #define kOnlyClassPtrD          typename std::enable_if<std::is_class<Class>::value, ptrdiff_t>::type

    // Utils
    //-------------------------------------
    template <class Class>
    using Method = void (Class:: *)();

    template <class Class>
    using ConstMethod = void (Class:: *)() const;

    //-------------------------------------
    template <class Class>
    inline Method<Class>
    getNonConstMethod(Method<Class> method) {
        return method;
    }

    //-------------------------------------
    template <class Class>
    inline ConstMethod<Class>
    getConstMethod(ConstMethod<Class> method) {
        return method;
    }

    //-------------------------------------
    template <typename T>
    class Delegate;

    //-------------------------------------
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
                virtual         ~Wrapper() { object = nullptr; func = nullptr;     }

                virtual void    operator()(const Args&... args) const {
                    if (object != nullptr)       ((object)->*(method))(args...);
                    else if (func != nullptr)    (*func)(args...);
                }

                void            ToBeRemoved() { object = nullptr, method = {}, isEnabled = false; }

                enum class Type { Unknown, Function, Method, Lambda };

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
                    if(Wrapper::func != nullptr)
                        (*Wrapper::func)(args...);
                }
            };

            //-----------------------------
            struct WrapperMethod : Wrapper {
                        WrapperMethod() { Wrapper::type = Wrapper::Type::Method; }

                void    operator()(const Args&... args) const override {
                    if(Wrapper::object != nullptr)
                        ((Wrapper::object)->*(Wrapper::method))(args...);
                }
            };

            // Lambdas with captures
            //-----------------------------
            template <typename Lambda>
            struct WrapperLambda : Wrapper {
                explicit WrapperLambda(const Lambda &l) : lambda(l) { Wrapper::type = Wrapper::Type::Lambda; }

                void    operator()(const Args&... args) const override {
                    if(Wrapper::isEnabled)
                        lambda(args...);
                }

                Lambda  lambda;
            };

        // Some helpers
        protected:
            template <class Class>
            static Class *  unconst(const Class *object)                                        { return const_cast<Class *>(object);                           }

        public:
                            Delegate() = default;
            virtual         ~Delegate();

            // avoid nullptr as lambda
            size_t          Add(std::nullptr_t)                                                 { return size_t(-1);                                            }

            size_t          Add(TFunc func);

            template <class Class>
            kOnlyClassSizeT Add(Class *object)                                                  { return Add(object, getNonConstMethod(&Class::operator()));    }
            template <class Class>
            kOnlyClassSizeT Add(const Class *object)                                            { return Add(object, getConstMethod(&Class::operator()));       }
            template <class Class>
            kOnlyClassSizeT Add(Class *object, kMethod(method));
            template <class Class>
            kOnlyClassSizeT Add(const Class *object, kMethod(method))                           { return Add(unconst(object), method);                          }
            template <class Class>
            kOnlyClassSizeT Add(Class *object, kMethod(method) const)                           { return Add(object, kUnConst(method));                         }
            template <class Class>
            kOnlyClassSizeT Add(const Class *object, kMethod(method) const)                     { return Add(unconst(object), kUnConst(method));                }
            // Mimic std::bind order
            template <class Class>
            kOnlyClassSizeT Add(kMethod(method), Class *object)                                 { return Add(object, method);                                   }
            template <class Class>
            kOnlyClassSizeT Add(kMethod(method), const Class *object)                           { return Add(unconst(object), method);                          }
            template <class Class>
            kOnlyClassSizeT Add(kMethod(method) const, Class *object)                           { return Add(object, kUnConst(method));                         }
            template <class Class>
            kOnlyClassSizeT Add(kMethod(method) const, const Class *object)                     { return Add(unconst(object), kUnConst(method));                }

            // Hack to detect lambdas with captures
            template <typename Lambda, typename std::enable_if<!std::is_assignable<Lambda, Lambda>::value, bool>::type = true>
            size_t          Add(const Lambda &lambda) {
                auto wrapper = new WrapperLambda<Lambda>(lambda);
                mWrappers.emplace_back(wrapper);
                return wrapper->id;
            }

            //--
            bool            Remove(std::nullptr_t, bool lazy=false)                             { return false;                                                 }

            bool            Remove(TFunc func, bool lazy=false)                                 { return RemoveIndex(Find(func), lazy);                         }

            template <class Class>
            bool            Remove(Class *object, bool lazy=false)                              { return RemoveIndex(Find(object, getNonConstMethod(&Class::operator())), lazy);        }
            template <class Class>
            bool            Remove(const Class *object, bool lazy=false)                        { return RemoveIndex(Find(unconst(object), getConstMethod(&Class::operator())), lazy);  }
            template <class Class>
            bool            Remove(Class *object, kMethod(method), bool lazy=false)             { return RemoveIndex(Find(object, method), lazy);               }
            template <class Class>
            bool            Remove(const Class *object, kMethod(method), bool lazy=false)       { return RemoveIndex(Find(unconst(object), method), lazy);      }
            template <class Class>
            bool            Remove(Class *object, kMethod(method) const, bool lazy=false)       { return RemoveIndex(Find(object, kUnConst(method)), lazy);     }
            template <class Class>
            bool            Remove(const Class *object, kMethod(method) const, bool lazy=false) { return RemoveIndex(Find(unconst(object), kUnConst(method)), lazy);    }
            // Mimic std::bind order
            template <class Class>
            bool            Remove(kMethod(method), Class *object, bool lazy=false)             { return RemoveIndex(Find(object, method), lazy);               }
            template <class Class>
            bool            Remove(kMethod(method), const Class *object, bool lazy=false)       { return RemoveIndex(Find(unconst(object), method), lazy);      }
            template <class Class>
            bool            Remove(kMethod(method) const, Class *object, bool lazy=false)       { return RemoveIndex(Find(object, kUnConst(method)), lazy);     }
            template <class Class>
            bool            Remove(kMethod(method) const, const Class *object, bool lazy=false) { return RemoveIndex(Find(unconst(object), kUnConst(method)), lazy);    }
            // Hack to detect lambdas with captures (and return a value)
            //template <typename Lambda, std::enable_if_t<!std::is_assignable_v<Lambda, Lambda>, bool> = true>
            //bool            Remove(const Lambda &l) {
            //    static_assert(false, "You cannot remove a complex lambda");
            //    return -1;
            //}

            //--
            bool            RemoveById(size_t id, bool lazy=false);

            //--
            void            RemoveLazyDeleted();

            void            Clear();

            // The issue with perfect forwarding in this context is that we can not pass rValues to more than one function.
            // So, we need the other version of operator() to pass const references.
            // In any case, the Wrappers cannot have both operators() because they are virtual functions,
            // and a template function cannot be virtual.
            template <typename Dummy = void>
            typename std::enable_if<sizeof...(Args) != 0, Dummy>::type
                            operator()(Args&&... args) const                                    { for (const auto *wrapper : mWrappers) (*wrapper)(std::forward<Args>(args)...); }

            void            operator()(const Args&... args) const                               { for (const auto *wrapper : mWrappers) (*wrapper)(args...);    }

            size_t          GetNumDelegates() const                                             { return mWrappers.size() - mToRemove.size();               }

        protected:
            ptrdiff_t       Find(std::nullptr_t)                                                { return -1;                                                }

            ptrdiff_t       Find(const TFunc func) const;

            template <class Class>
            kOnlyClassPtrD  Find(Class *object) const                                           { return Find(object, getNonConstMethod(&Class::operator()));   }
            template <class Class>
            kOnlyClassPtrD  Find(const Class *object) const                                     { return Find(object, getConstMethod(&Class::operator()));      }
            template <class Class>
            kOnlyClassPtrD  Find(Class *object, kMethod(method)) const;
            template <class Class>
            kOnlyClassPtrD  Find(Class *object, kMethod(method) const) const                    { return Find(object, kUnConst(method));                    }
            template <class Class>
            kOnlyClassPtrD  Find(const Class *object, kMethod(method)) const                    { return Find(unconst(object), method);                     }
            template <class Class>
            kOnlyClassPtrD  Find(const Class *object, kMethod(method) const) const              { return Find(unconst(object), kUnConst(method));           }
            template <class Class>
            kOnlyClassPtrD  Find(kMethod(method), Class *object) const                          { return Find(object, method);                              }
            template <class Class>
            kOnlyClassPtrD  Find(kMethod(method) const, Class *object) const                    { return Find(object, kUnConst(method));                    }
            template <class Class>
            kOnlyClassPtrD  Find(kMethod(method), const Class *object) const                    { return Find(unconst(object), method);                     }
            template <class Class>
            kOnlyClassPtrD  Find(kMethod(method) const, const Class *object) const              { return Find(unconst(object), kUnConst(method));           }
            // Hack to detect lambdas with captures (and return a value)
            //template <typename Lambda, std::enable_if_t<!std::is_assignable_v<Lambda, Lambda>, bool> = true>
            //ptrdiff_t       Find(const Lambda &l) {
            //    static_assert(false, "You cannot find a complex lambda");
            //    return -1;
            //}

        protected:
            bool            RemoveIndex(ptrdiff_t idx, bool lazy);

        protected:
            std::vector<Wrapper *> mWrappers;
            std::vector<size_t>    mToRemove;
    };

    //-------------------------------------
    template <typename ...Args>
    size_t Delegate<void(Args...)>::wrapperCounter = 0;

    //-------------------------------------
    template <typename ...Args>
    inline
    Delegate<void(Args...)>::~Delegate() {
        for (auto &wrapper: mWrappers) {
            delete wrapper;
        }
        mWrappers.clear();
        mToRemove.clear();
    }

    //-------------------------------------
    template <typename ...Args>
    inline size_t
    Delegate<void(Args...)>::Add(TFunc func) {
        if(func != nullptr) {
            auto wrapper = new WrapperCFunc(func);
            mWrappers.emplace_back(wrapper);
            return wrapper->id;
        }

        return size_t(-1);
    }

    //-------------------------------------
    template <typename ...Args>
    template <class Class>
    inline kOnlyClassSizeT
    Delegate<void(Args...)>::Add(Class *object, void(Class::*method)(Args...)) {
        if(object == nullptr || method == nullptr)
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

    //-------------------------------------
    template <typename ...Args>
    inline bool
    Delegate<void(Args...)>::RemoveById(size_t id, bool lazy) {
        for(size_t i=0; i<mWrappers.size(); ++i) {
            if(mWrappers[i]->id == id) {
                return RemoveIndex(i, lazy);
            }
        }

        return false;
    }

    //-------------------------------------
    template <typename ...Args>
    inline bool
    Delegate<void(Args...)>::RemoveIndex(ptrdiff_t idx, bool lazy) {
        if(idx >= 0) {
            if(lazy == false) {
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

    //-------------------------------------
    template <typename ...Args>
    inline ptrdiff_t
    Delegate<void(Args...)>::Find(const TFunc func) const {
        ptrdiff_t   i = 0;

        for(const auto *wrapper : mWrappers) {
            if(wrapper->func == func) {
                return i;
            }
            ++i;
        }
        return -1;
    }

    //-------------------------------------
    template <typename ...Args>
    template <class Class>
    inline kOnlyClassPtrD
    Delegate<void(Args...)>::Find(Class *object, void(Class::*method)(Args...)) const {
        ptrdiff_t   i = 0;

        for(const auto *wrapper : mWrappers) {
            if(wrapper->object == reinterpret_cast<UnknownClass *>(object)) {
    #if defined(_MSC_VER)
                if(memcmp(&wrapper->method, reinterpret_cast<void *>(&method), sizeof(method)) == 0) {
    #else
                if(wrapper->method == reinterpret_cast<TMethod>(method)) {
    #endif
                    return i;
                }
            }
            ++i;
        }

        return -1;
    }

    //-------------------------------------
    template <typename ...Args>
    inline void
    Delegate<void(Args...)>::RemoveLazyDeleted() {
        ptrdiff_t   i, size;

        std::sort(mToRemove.begin(), mToRemove.end());
        size = mToRemove.size() - 1;
        for(i=size; i>=0; --i) {
            delete mWrappers[mToRemove[i]];
            mWrappers.erase(mWrappers.begin() + mToRemove[i]);
        }
        mToRemove.clear();
    }

    //-------------------------------------
    template <typename ...Args>
    inline void
    Delegate<void(Args...)>::Clear() {
        for(auto *w : mWrappers) {
            delete w;
        }
        mWrappers.clear();
        mToRemove.clear();
    }

    #undef kUnConst
    #undef kMethod
    #undef kOnlyClassSizeT
    #undef kOnlyClassPtrD

} // end of namespace
