//-----------------------------------------------------------------------------
// Several game systems that do not know each other.
// When an enemy dies, the enemies only send a notification,
// and the score, the achievements and the audio react to it.
// Adding or removing a system does not change any other system
//-----------------------------------------------------------------------------

#include "notifications/NotificationManager.h"
#include "NotificationId.h"
#include <cstdio>
#include <string>
#include <vector>

using MindShake::NotificationManager;
using MindShake::NotificationId;

//-------------------------------------
struct EnemyKilled {
    std::string type;
    int         points;
    float       x;
    float       y;
};

//-------------------------------------
class Enemies {
    public:
        // A fixed script, so the output is always the same
        void Update(int frame) {
            if (frame == 1) {
                Kill({ "Goblin", 10, 2.0f, 3.0f });
            }
            else if (frame == 2) {
                Kill({ "Orc", 25, 5.0f, 1.0f });
                Kill({ "Orc", 25, 6.0f, 1.5f });
            }
            else if (frame == 4) {
                Kill({ "Dragon", 100, 9.0f, 9.0f });
            }
        }

    private:
        void Kill(const EnemyKilled &enemy) {
            printf("  %s killed\n", enemy.type.c_str());
            NotificationManager::SendNotification(NotificationId::EnemyKilled, enemy);
        }
};

//-------------------------------------
class ScoreBoard {
    public:
        ScoreBoard()  { NotificationManager::GetDelegate(NotificationId::EnemyKilled).Add(this, &ScoreBoard::OnEnemyKilled);    }
        ~ScoreBoard() { NotificationManager::GetDelegate(NotificationId::EnemyKilled).Remove(this, &ScoreBoard::OnEnemyKilled); }

    private:
        void OnEnemyKilled(NotificationId, const any &data) {
            mScore += any_cast<const EnemyKilled &>(data).points;

            // Only the last score of the frame matters, so it replaces the one still pending
            NotificationManager::SendNotification(NotificationId::ScoreChanged, mScore, true);
        }

        int mScore = 0;
};

//-------------------------------------
class Achievements {
    public:
        Achievements()  { NotificationManager::GetDelegate(NotificationId::EnemyKilled).Add(this, &Achievements::OnEnemyKilled);    }
        ~Achievements() { NotificationManager::GetDelegate(NotificationId::EnemyKilled).Remove(this, &Achievements::OnEnemyKilled); }

    private:
        void OnEnemyKilled(NotificationId, const any &data) {
            ++mKills;
            if (mKills == 1) {
                printf("  Achievement unlocked: First blood\n");
            }
            if (any_cast<const EnemyKilled &>(data).type == "Dragon") {
                printf("  Achievement unlocked: Dragon slayer\n");
            }
        }

        int mKills = 0;
};

//-------------------------------------
class Audio {
    public:
        Audio() {
            mId = NotificationManager::GetDelegate(NotificationId::EnemyKilled).Add([](NotificationId, const any &data) {
                const EnemyKilled &enemy = any_cast<const EnemyKilled &>(data);
                printf("  Audio: explosion at (%.1f, %.1f)\n", enemy.x, enemy.y);
            });
        }

        ~Audio() { NotificationManager::GetDelegate(NotificationId::EnemyKilled).RemoveById(mId); }

    private:
        size_t mId;
};

//-------------------------------------
class Hud {
    public:
        Hud()  { NotificationManager::GetDelegate(NotificationId::ScoreChanged).Add(this, &Hud::OnScoreChanged);    }
        ~Hud() { NotificationManager::GetDelegate(NotificationId::ScoreChanged).Remove(this, &Hud::OnScoreChanged); }

    private:
        void OnScoreChanged(NotificationId, const any &data) {
            printf("  HUD: score %d\n", any_cast<int>(data));
        }
};

//-------------------------------------
static void
RunGame() {
    Enemies      enemies;
    ScoreBoard   scoreBoard;
    Achievements achievements;
    Audio        audio;
    Hud          hud;

    for (int frame = 1; frame <= 4; ++frame) {
        printf("Frame %d\n", frame);

        enemies.Update(frame);

        // The score, the achievements and the audio react to the enemies killed in this frame
        NotificationManager::ReceivePendingNotifications();

        // The HUD receives the ScoreChanged notifications sent above. With overwrite, only the last one is left
        NotificationManager::ReceivePendingNotifications();
    }
}

//-------------------------------------
int
main() {
    // The notifications of a frame are delivered together, at fixed points of the frame
    NotificationManager::DisableAutoSend();

    // The systems remove their functions when they are destroyed, at the end of RunGame
    RunGame();

    NotificationManager::Clear();
    return 0;
}
