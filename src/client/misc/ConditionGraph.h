#pragma once
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace SDK {
    class Actor;
    class Player;
    struct MoveInputComponent;
}

enum class CondKind : uint8_t {
    Invalid = 0,
    And,
    Or,
    Not,
    Wait,
    HoldingItem,
    Health,
    Hunger,
    FallDistance,
    RiseDistance,
    SwitchToItem,
    TookDamage,
    DealtDamage,
    EnemiesInRange,
    Sprinting,
    Walking,
    Sneaking,
    OnGround,
    Count
};

enum class CondCompare : uint8_t {
    Less = 0,
    LessEqual,
    Greater,
    GreaterEqual,
    Count
};

struct CondNode {
    int id = 0;
    CondKind kind = CondKind::Invalid;

    std::vector<int> inputs;

    float x = 0.f;
    float y = 0.f;

    CondCompare compare = CondCompare::GreaterEqual;
    float threshold = 0.f;
    bool percent = false;
    float range = 8.f;
    float windowMs = 500.f;
    float waitMs = 250.f;
    int count = 1;
    bool invert = false;
    std::string itemId;
};

class ConditionRuntime {
public:
    void onTickSample(SDK::Player* lp);
    void onAttack(SDK::Actor* target);
    void reset();

    [[nodiscard]] float tookDamageAgeMs() const;
    [[nodiscard]] float dealtDamageAgeMs() const;
    [[nodiscard]] float riseDistance() const;

private:
    struct TargetSample {
        uint64_t runtimeId = 0;
        float health = 0.f;
        int32_t staleTicks = 0;
    };

    float lastSelfHealth = -1.f;
    std::chrono::steady_clock::time_point tookDamageAt {};
    std::chrono::steady_clock::time_point dealtDamageAt {};
    bool hasTookDamage = false;
    bool hasDealtDamage = false;
    float lastY = 0.f;
    float riseDist = 0.f;
    bool hasRiseDistance = false;
    std::vector<TargetSample> targets;
};

class CondEvalContext {
public:
    void reset(SDK::Player* lp);

    [[nodiscard]] SDK::Player* getPlayer() const { return player; }

    std::optional<float> health();
    std::optional<float> maxHealth();
    std::optional<float> hunger();
    std::optional<float> fallDistance();
    bool sprinting();
    bool walking();
    bool sneaking();
    bool onGround();
    std::string const& heldItemId();
    int enemiesWithin(float range);

private:
    SDK::MoveInputComponent* moveInputComponent();
    void buildEnemies();

    SDK::Player* player = nullptr;

    std::optional<float> vHealth;
    std::optional<float> vMaxHealth;
    std::optional<float> vHunger;
    std::optional<float> vFall;
    bool vSprint = false;
    bool vWalk = false;
    bool vSneak = false;
    bool vGround = false;
    std::string vHeld;

    bool dHealth = false;
    bool dMaxHealth = false;
    bool dHunger = false;
    bool dFall = false;
    bool dMove = false;
    bool dGround = false;
    bool dHeld = false;
    bool dEnemies = false;

    SDK::MoveInputComponent* moveInput = nullptr;
    bool dMoveInput = false;

    std::vector<float> enemyDistSq;
};

class ConditionGraph {
public:
    static constexpr int maxNodes = 64;

    [[nodiscard]] std::vector<CondNode>& getNodes() { return nodes; }
    [[nodiscard]] std::vector<CondNode> const& getNodes() const { return nodes; }
    [[nodiscard]] int getRoot() const { return root; }
    void setRoot(int id);

    [[nodiscard]] bool empty() const { return nodes.empty(); }
    [[nodiscard]] int findIndex(int id) const;
    [[nodiscard]] CondNode* find(int id);
    [[nodiscard]] CondNode const* find(int id) const;

    int addNode(CondKind kind, float x, float y);
    bool removeNode(int id);
    bool connect(int parentId, int childId);
    bool connectAt(int parentId, int childId, int index);
    bool disconnect(int parentId, int childId);
    [[nodiscard]] int parentOf(int childId) const;
    [[nodiscard]] bool wouldCycle(int parentId, int childId) const;

    [[nodiscard]] static int minArity(CondKind kind);
    [[nodiscard]] static int maxArity(CondKind kind);
    [[nodiscard]] static bool isLogic(CondKind kind);
    [[nodiscard]] static bool usesThreshold(CondKind kind);
    [[nodiscard]] static char const* kindKey(CondKind kind);
    [[nodiscard]] static CondKind kindFromKey(std::string const& key);

    bool evaluate(CondEvalContext& ctx, ConditionRuntime& rt);
    void resetRuntime() { waitStates.clear(); }

    [[nodiscard]] nlohmann::json toJson() const;
    void fromJson(nlohmann::json const& j);

    void sanitize();

private:
    void breakCycles();
    [[nodiscard]] int cachedIndexOf(int id) const;
    void buildIndexCache();

    std::vector<CondNode> nodes;
    int root = 0;
    int nextId = 1;
    bool indexDirty = true;

    struct WaitState {
        std::chrono::steady_clock::time_point started {};
        bool active = false;
    };

    std::unordered_map<int, WaitState> waitStates;
    std::vector<std::pair<int, int>> evalIndex;
    std::vector<uint8_t> evalState;
    std::vector<uint8_t> evalValue;
    std::vector<int> evalStack;
};
