#pragma once

#include <cstdint>
#include <random>
#include <functional>
#include <chrono>
#include <string>
#include "notifications/NotificationManager.h"
#include "NotificationId.h"

//-------------------------------------
using MindShake::NotificationManager;
using MindShake::NotificationId;

//-------------------------------------
constexpr uint32_t  kMaxValue = 16;

//-------------------------------------
std::default_random_engine              gGenerator;
std::uniform_int_distribution<uint32_t> gDistribution(1, kMaxValue);
auto                                    getRandom = std::bind(gDistribution, gGenerator);

//-------------------------------------
struct Agent {
    using time_point = std::chrono::time_point<std::chrono::system_clock>;

    Agent(const std::string &name, float hp = 10.0f, float damage = 1, float defense = 1, uint32_t speed = 250)
        : mName(name), mMaxHP(hp), mHP(hp), mDamage(damage), mDefense(defense), mSpeed(speed) {
        }

    bool
    Run() {
        char buffer[256];

        if (mHP < 0)
            return false;

        if(Time() - mTimeStamp > std::chrono::milliseconds(mSpeed)) {
            mTimeStamp = Time();
            if(mEnemy != nullptr) {
                if(true) {//DiceRoller() > 5) {
                    float damage = float(DiceRoller()) / kMaxValue * (mDamage * mDamage) / (mDamage + mEnemy->mDefense);
                    snprintf(buffer, sizeof(buffer), "%s hits %s causing %.2f damage.", mName.c_str(), mEnemy->mName.c_str(), damage);
                    NotificationManager::SendNotification(NotificationId::Log, std::string(buffer));
                    mEnemy->mHP -= damage;
                    if(mEnemy->mHP < 0) {
                        snprintf(buffer, sizeof(buffer), "%s is dead.", mEnemy->mName.c_str());
                        NotificationManager::SendNotification(NotificationId::Log, std::string(buffer));
                        NotificationManager::SendNotification(NotificationId::Kill, mEnemy);
                    }

                    return true;
                }
            }
        }

        return false;
    };

    void                SetEnemy(Agent *other) { mEnemy = other; }

    static uint32_t     DiceRoller() {
        return getRandom();
    }

    static time_point   Time() {
        return std::chrono::system_clock::now();
    }

    std::string mName;
    float       mMaxHP;
    float       mHP;
    float       mDamage;
    float       mDefense;
    uint32_t    mSpeed;
    time_point  mTimeStamp {};
    Agent       *mEnemy    {};
};
