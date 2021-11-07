#pragma once

//-----------------------------------------------------------------------------
// Copyright (C) 2021 Carlos Aragonés
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt
//-----------------------------------------------------------------------------

#include <cstdint>
#include <vector>
//#include <utility>
#include <type_traits>
#include <algorithm>

//-------------------------------------
namespace MindShake {

    //-------------------------------------
    class UnknownClass;

    //-------------------------------------
    template <typename T>
    class Delegate;

    //-------------------------------------
    template <typename ...Args>
    class Delegate<void(Args...)> {
        public:
            typedef void (              *TFunc)(Args...);
            typedef void (UnknownClass::*TMethod)(Args...);

        protected:
            //-----------------------------
            struct Wrapper {
                                Wrapper() = default;
                                Wrapper(TFunc f) : func(f) {}
                virtual         ~Wrapper() { object = nullptr; func = nullptr;     }

                virtual void    operator()(Args&&... args) const {
                    if(object != nullptr)       ((object)->*(method))(std::forward<Args>(args)...);
                    else if(func != nullptr)    (*func)(std::forward<Args>(args)...);
                }

                void            ToBeRemoved() { object = nullptr, method = {}, isEnabled = false; }

                enum class Type { Unknown, Function, Method, Lambda };

                //--
                UnknownClass    *object {};
                union {
                    TMethod      method {};
                    TFunc        func;
                };
                Type    type = Type::Unknown;
                bool    isEnabled = true;
            };

            //-----------------------------
            struct WrapperCFunc : Wrapper {
                        WrapperCFunc(TFunc f) : Wrapper(f) { Wrapper::type = Wrapper::Type::Function; }

                void    operator()(Args&&... args) const override {
                    if(Wrapper::func != nullptr)
                        (*Wrapper::func)(std::forward<Args>(args)...);
                }
            };

            //-----------------------------
            struct WrapperMethod : Wrapper {
                        WrapperMethod() { Wrapper::type = Wrapper::Type::Method; }

                void    operator()(Args&&... args) const override {
                    if(Wrapper::object != nullptr)
                        ((Wrapper::object)->*(Wrapper::method))(std::forward<Args>(args)...);
                }
            };

            // Lambdas with captures
            //-----------------------------
            template <typename Lambda>
            struct WrapperLambda : Wrapper {
                WrapperLambda(const Lambda &l) : lambda(l) { Wrapper::type = Wrapper::Type::Lambda; }

                void    operator()(Args&&... args) const override {
                    if(Wrapper::isEnabled)
                        lambda(std::forward<Args>(args)...);
                }

                Lambda  lambda;
            };

        // Some helpers
        protected:
            template <class Class>
            Class *         unconst(const Class *object)                                        { return const_cast<Class *>(object);                   }
            // I cannot do this with a method
            #define kUnConst(method)                                                            reinterpret_cast<void(Class::*)(Args...)>(method)
            #define kMethod(method)                                                             void(Class::*method)(Args...)

        public:
                            Delegate() = default;
                            ~Delegate();

            void            Add(std::nullptr_t)                                                 { } // avoid nullptr as lambda
            void            Add(TFunc func)                                                     { if(func) mWrappers.emplace_back(new WrapperCFunc(func));                }
            template <class Class>
            void            Add(Class *object)                                                  { Add(object, &Class::operator());                      }
            template <class Class>
            void            Add(Class *object, kMethod(method));
            template <class Class>
            void            Add(Class *object, kMethod(method) const)                           { Add(object, kUnConst(method));                        }
            template <class Class>
            void            Add(const Class *object)                                            { Add(unconst(object), &Class::operator());             }
            template <class Class>
            void            Add(const Class *object, kMethod(method))                           { Add(unconst(object), method);                         }
            template <class Class>
            void            Add(const Class *object, kMethod(method) const)                     { Add(unconst(object), method);                         }
            // Hack to detect lambdas with captures
            template <typename Lambda>
            typename std::enable_if<!std::is_assignable<Lambda, Lambda>::value>::type   // Hack to detect lambdas with captures
                            Add(const Lambda &lambda)                                           { mWrappers.emplace_back(new WrapperLambda<Lambda>(lambda)); }

            //--
            bool            Remove(std::nullptr_t, bool lazy=false)                             { return false;                                         }
            bool            Remove(TFunc func, bool lazy=false)                                 { return RemoveIndex(Find(func), lazy);                 }
            template <class Class>
            bool            Remove(Class *object, bool lazy=false)                              { return Remove(object, &Class::operator(), lazy);      }
            template <class Class>
            bool            Remove(Class *object, kMethod(method), bool lazy=false)             { return RemoveIndex(Find(object, method), lazy);       }
            template <class Class>
            bool            Remove(Class *object, kMethod(method) const, bool lazy=false)       { return Remove(object, kUnConst(method), lazy);        }
            template <class Class>
            bool            Remove(const Class *object, bool lazy=false)                        { return Remove(unconst(object), &Class::operator(), lazy); }
            template <class Class>
            bool            Remove(const Class *object, kMethod(method), bool lazy=false)       { return Remove(unconst(object, method, lazy));         }
            template <class Class>
            bool            Remove(const Class *object, kMethod(method) const, bool lazy=false) { return Remove(unconst(object), kUnConst(method), lazy);   }
            // Hack to detect lambdas with captures (and return a value)
            //template <typename Lambda, std::enable_if_t<!std::is_assignable_v<Lambda, Lambda>, bool> = true>
            //bool            Remove(const Lambda &l) {
            //    static_assert(false, "You cannot remove a complex lambda");
            //    return -1;
            //}

            //--
            void            RemoveLazyDeleted();

            //--
            ptrdiff_t       Find(TFunc func) const;
            template <class Class>
            ptrdiff_t       Find(Class *object) const                                           { return Find(object, &Class::operator());                  }
            template <class Class>
            ptrdiff_t       Find(Class *object, kMethod(method)) const;
            template <class Class>
            ptrdiff_t       Find(Class *object, kMethod(method) const) const                    { return Find(object, kUnConst(method));                    }
            template <class Class>
            ptrdiff_t       Find(const Class *object) const                                     { return Find(unconst(object), &Class::operator());         }
            template <class Class>
            ptrdiff_t       Find(const Class *object, kMethod(method)) const                    { return Find(unconst(object, method));                     }
            template <class Class>
            ptrdiff_t       Find(const Class *object, kMethod(method) const) const              { return Find(unconst(object), kUnConst(method));           }
            // Hack to detect lambdas with captures (and return a value)
            //template <typename Lambda, std::enable_if_t<!std::is_assignable_v<Lambda, Lambda>, bool> = true>
            //ptrdiff_t       Find(const Lambda &l) {
            //    static_assert(false, "You cannot find a complex lambda");
            //    return -1;
            //}

            void            Clear();

            void            operator()(Args... args) const                                      { for (auto wrapper : mWrappers) (*wrapper)(std::forward<Args>(args)...);    }

            size_t          GetNumDelegates() const                                             { return mWrappers.size() - mToRemove.size();           }

            #undef kUnConst
            #undef kMethod

        protected:
            bool            RemoveIndex(ptrdiff_t idx, bool lazy);

        protected:
            std::vector<Wrapper *> mWrappers;
            std::vector<size_t>    mToRemove;
    };

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
    template <class Class>
    inline void
    Delegate<void(Args...)>::Add(Class *object, void(Class::*method)(Args...)) {
        if(object == nullptr || method == nullptr)
            return;

        WrapperMethod   *wrapper = new WrapperMethod;

        wrapper->object   = reinterpret_cast<UnknownClass *>(object);

    #if defined(_MSC_VER)
        memset(((void *) &wrapper->method), 0, sizeof(TMethod));
        memcpy(((void *) &wrapper->method), ((void *) &method), sizeof(method));
    #else
        wrapper->method  = reinterpret_cast<TMethod>(method);
    #endif

        mWrappers.emplace_back(wrapper);
    }

    //-------------------------------------
    template <typename ...Args>
    bool
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
    ptrdiff_t
    Delegate<void(Args...)>::Find(TFunc func) const {
        ptrdiff_t   i = 0;

        for(auto &wrapper : mWrappers) {
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
    ptrdiff_t
    Delegate<void(Args...)>::Find(Class *object, void(Class::*method)(Args...)) const {
        ptrdiff_t   i = 0;

        for(auto &wrapper : mWrappers) {
            if(wrapper->object == reinterpret_cast<UnknownClass *>(object)) {
    #if defined(_MSC_VER)
                if(memcmp(&wrapper->method, (void *) &method, sizeof(method)) == 0) {
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
        for(auto &w : mWrappers) {
            delete w;
        }
        mWrappers.clear();
        mToRemove.clear();
    }

} // end of namespace
