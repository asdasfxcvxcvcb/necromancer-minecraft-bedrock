#include "pch.h"
#include "ConditionGraph.h"
#include "EntityCache.h"
#include "mc/common/entity/component/FallDistanceComponent.h"
#include "mc/common/entity/component/MoveInputComponent.h"
#include "mc/common/world/actor/player/Player.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
    constexpr float minWindowMs = 50.f;
    constexpr float maxWindowMs = 10000.f;
    constexpr float maxRange = 128.f;
    constexpr float maxThreshold = 10000.f;
    constexpr float maxWaitMs = 600000.f;
    constexpr int maxEnemyCount = 256;

    struct KindInfo {
        CondKind kind;
        char const* key;
        int minArity;
        int maxArity;
    };

    constexpr KindInfo kindTable[] = {
        { CondKind::And, "and", 2, 8 },
        { CondKind::Or, "or", 2, 8 },
        { CondKind::Not, "not", 1, 1 },
        { CondKind::Wait, "wait", 1, 1 },
        { CondKind::HoldingItem, "holdingItem", 0, 0 },
        { CondKind::Health, "health", 0, 0 },
        { CondKind::Hunger, "hunger", 0, 0 },
        { CondKind::FallDistance, "fallDistance", 0, 0 },
        { CondKind::RiseDistance, "riseDistance", 0, 0 },
        { CondKind::SwitchToItem, "switchToItem", 0, 0 },
        { CondKind::TookDamage, "tookDamage", 0, 0 },
        { CondKind::DealtDamage, "dealtDamage", 0, 0 },
        { CondKind::EnemiesInRange, "enemiesInRange", 0, 0 },
        { CondKind::Sprinting, "sprinting", 0, 0 },
        { CondKind::Walking, "walking", 0, 0 },
        { CondKind::Sneaking, "sneaking", 0, 0 },
        { CondKind::OnGround, "onGround", 0, 0 },
    };

    KindInfo const* infoOf(CondKind kind) {
        for (auto const& info : kindTable) {
            if (info.kind == kind) return &info;
        }
        return nullptr;
    }

    bool compareValue(CondCompare cmp, float value, float threshold) {
        switch (cmp) {
        case CondCompare::Less:
            return value < threshold;
        case CondCompare::LessEqual:
            return value <= threshold;
        case CondCompare::Greater:
            return value > threshold;
        case CondCompare::GreaterEqual:
            return value >= threshold;
        default:
            return false;
        }
    }
}

void ConditionRuntime::reset() {
    lastSelfHealth = -1.f;
    hasTookDamage = false;
    hasDealtDamage = false;
    riseDist = 0.f;
    lastY = 0.f;
    hasRiseDistance = false;
    targets.clear();
}

void ConditionRuntime::onTickSample(SDK::Player* lp) {
    if (!lp) {
        reset();
        return;
    }

    auto health = lp->getHealth();
    if (health) {
        if (lastSelfHealth >= 0.f && *health < lastSelfHealth - 0.01f) {
            tookDamageAt = std::chrono::steady_clock::now();
            hasTookDamage = true;
        }
        lastSelfHealth = *health;
    } else {
        lastSelfHealth = -1.f;
    }

    Vec3 pos = lp->getPos();
    if (hasRiseDistance) {
        float deltaY = pos.y - lastY;
        if (deltaY > 0.f) riseDist += deltaY;
        else if (deltaY < -0.001f) riseDist = 0.f;
    }
    lastY = pos.y;
    hasRiseDistance = true;

    if (targets.empty()) return;

    auto snap = EntityCache::get().snapshot();
    for (auto it = targets.begin(); it != targets.end();) {
        auto const* view = snap ? snap->findView(it->runtimeId) : nullptr;
        if (!view || !view->hasHealth) {
            it->staleTicks++;
            if (it->staleTicks > 40) {
                it = targets.erase(it);
                continue;
            }
            ++it;
            continue;
        }

        if (view->health < it->health - 0.01f) {
            dealtDamageAt = std::chrono::steady_clock::now();
            hasDealtDamage = true;
        }
        it->health = view->health;
        it->staleTicks++;
        if (it->staleTicks > 200) {
            it = targets.erase(it);
            continue;
        }
        ++it;
    }
}

void ConditionRuntime::onAttack(SDK::Actor* target) {
    if (!target) return;

    uint64_t id = target->getRuntimeID();
    float health = target->getHealth().value_or(-1.f);
    if (health < 0.f) return;

    for (auto& sample : targets) {
        if (sample.runtimeId != id) continue;
        sample.health = health;
        sample.staleTicks = 0;
        return;
    }

    if (targets.size() >= 16) {
        auto oldest = std::ranges::max_element(targets, {}, &TargetSample::staleTicks);
        if (oldest != targets.end()) targets.erase(oldest);
    }
    targets.push_back({ id, health, 0 });
}

float ConditionRuntime::tookDamageAgeMs() const {
    if (!hasTookDamage) return std::numeric_limits<float>::max();
    auto delta = std::chrono::steady_clock::now() - tookDamageAt;
    return std::chrono::duration<float, std::milli>(delta).count();
}

float ConditionRuntime::dealtDamageAgeMs() const {
    if (!hasDealtDamage) return std::numeric_limits<float>::max();
    auto delta = std::chrono::steady_clock::now() - dealtDamageAt;
    return std::chrono::duration<float, std::milli>(delta).count();
}

float ConditionRuntime::riseDistance() const {
    return riseDist;
}

void CondEvalContext::reset(SDK::Player* lp) {
    player = lp;
    dHealth = dMaxHealth = dHunger = dFall = dMove = dGround = dHeld = dEnemies = false;
    dMoveInput = false;
    moveInput = nullptr;
    enemyDistSq.clear();
}

std::optional<float> CondEvalContext::health() {
    if (!dHealth) {
        dHealth = true;
        vHealth = player ? player->getHealth() : std::nullopt;
    }
    return vHealth;
}

std::optional<float> CondEvalContext::maxHealth() {
    if (!dMaxHealth) {
        dMaxHealth = true;
        vMaxHealth = player ? player->getMaxHealth() : std::nullopt;
    }
    return vMaxHealth;
}

std::optional<float> CondEvalContext::hunger() {
    if (!dHunger) {
        dHunger = true;
        vHunger = player ? player->getHunger() : std::nullopt;
    }
    return vHunger;
}

std::optional<float> CondEvalContext::fallDistance() {
    if (!dFall) {
        dFall = true;
        vFall = std::nullopt;
        if (player) {
            if (auto* comp = player->tryGetComponent<SDK::FallDistanceComponent>()) {
                vFall = comp->fallDistance;
            }
        }
    }
    return vFall;
}

SDK::MoveInputComponent* CondEvalContext::moveInputComponent() {
    if (!dMoveInput) {
        dMoveInput = true;
        moveInput = player ? player->getMoveInputComponent() : nullptr;
    }
    return moveInput;
}

bool CondEvalContext::sprinting() {
    if (!dMove) {
        dMove = true;
        vSprint = false;
        vWalk = false;
        vSneak = false;
        if (auto* comp = moveInputComponent()) {
            vSprint = comp->sprinting;
            vSneak = comp->sneaking;
            float mx = comp->move.x;
            float my = comp->move.y;
            vWalk = (mx * mx + my * my) > 0.0001f;
        }
    }
    return vSprint;
}

bool CondEvalContext::walking() {
    sprinting();
    return vWalk && !vSprint;
}

bool CondEvalContext::sneaking() {
    sprinting();
    return vSneak;
}

bool CondEvalContext::onGround() {
    if (!dGround) {
        dGround = true;
        vGround = player ? player->isOnGround() : false;
    }
    return vGround;
}

std::string const& CondEvalContext::heldItemId() {
    if (!dHeld) {
        dHeld = true;
        vHeld.clear();
        if (player && player->supplies && player->supplies->inventory) {
            int slot = player->supplies->selectedSlot;
            if (slot >= 0 && slot < 9) {
                if (auto* stack = player->supplies->inventory->getItem(slot)) {
                    if (auto* item = stack->getItem()) {
                        vHeld = item->namespacedId.getString();
                    }
                }
            }
        }
    }
    return vHeld;
}

void CondEvalContext::buildEnemies() {
    dEnemies = true;
    enemyDistSq.clear();
    if (!player) return;

    auto snap = EntityCache::get().snapshot();
    if (!snap) return;

    Vec3 self = player->getPos();
    uint64_t selfId = player->getRuntimeID();

    enemyDistSq.reserve(snap->views.size());
    for (auto const& view : snap->views) {
        if (!view.actor || view.runtimeId == selfId) continue;
        if (view.kind != EntityCache::EntKind::Player && view.kind != EntityCache::EntKind::Mob) continue;
        if (!view.hasHealth || view.health <= 0.f) continue;

        Vec3 pos = view.actor->getPos();
        float dx = pos.x - self.x;
        float dy = pos.y - self.y;
        float dz = pos.z - self.z;
        enemyDistSq.push_back(dx * dx + dy * dy + dz * dz);
    }
}

int CondEvalContext::enemiesWithin(float range) {
    if (!dEnemies) buildEnemies();

    float limit = std::clamp(range, 0.f, maxRange);
    float limitSq = limit * limit;
    int found = 0;
    for (float distSq : enemyDistSq) {
        if (distSq <= limitSq) found++;
    }
    return found;
}

bool ConditionGraph::isLogic(CondKind kind) {
    return kind == CondKind::And || kind == CondKind::Or || kind == CondKind::Not || kind == CondKind::Wait;
}

bool ConditionGraph::usesThreshold(CondKind kind) {
    return kind == CondKind::Health || kind == CondKind::Hunger || kind == CondKind::FallDistance || kind == CondKind::RiseDistance;
}

int ConditionGraph::minArity(CondKind kind) {
    auto const* info = infoOf(kind);
    return info ? info->minArity : 0;
}

int ConditionGraph::maxArity(CondKind kind) {
    auto const* info = infoOf(kind);
    return info ? info->maxArity : 0;
}

char const* ConditionGraph::kindKey(CondKind kind) {
    auto const* info = infoOf(kind);
    return info ? info->key : "";
}

CondKind ConditionGraph::kindFromKey(std::string const& key) {
    for (auto const& info : kindTable) {
        if (key == info.key) return info.kind;
    }
    return CondKind::Invalid;
}

int ConditionGraph::findIndex(int id) const {
    for (size_t i = 0; i < nodes.size(); i++) {
        if (nodes[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

CondNode* ConditionGraph::find(int id) {
    int index = findIndex(id);
    return index >= 0 ? &nodes[index] : nullptr;
}

CondNode const* ConditionGraph::find(int id) const {
    int index = findIndex(id);
    return index >= 0 ? &nodes[index] : nullptr;
}

void ConditionGraph::setRoot(int id) {
    root = findIndex(id) >= 0 ? id : 0;
    resetRuntime();
}

int ConditionGraph::addNode(CondKind kind, float x, float y) {
    if (kind == CondKind::Invalid || kind >= CondKind::Count) return 0;
    if (nodes.size() >= static_cast<size_t>(maxNodes)) return 0;

    CondNode node;
    node.id = nextId++;
    node.kind = kind;
    node.x = x;
    node.y = y;

    switch (kind) {
    case CondKind::Health:
        node.compare = CondCompare::LessEqual;
        node.threshold = 50.f;
        node.percent = true;
        break;
    case CondKind::Hunger:
        node.compare = CondCompare::LessEqual;
        node.threshold = 6.f;
        break;
    case CondKind::FallDistance:
        node.compare = CondCompare::GreaterEqual;
        node.threshold = 3.f;
        break;
    case CondKind::RiseDistance:
        node.compare = CondCompare::GreaterEqual;
        node.threshold = 3.f;
        break;
    case CondKind::Wait:
        node.waitMs = 250.f;
        break;
    case CondKind::EnemiesInRange:
        node.range = 8.f;
        node.count = 1;
        break;
    default:
        break;
    }

    nodes.push_back(std::move(node));
    indexDirty = true;
    if (root == 0) root = nodes.back().id;
    return nodes.back().id;
}

bool ConditionGraph::removeNode(int id) {
    int index = findIndex(id);
    if (index < 0) return false;

    nodes.erase(nodes.begin() + index);
    resetRuntime();
    indexDirty = true;
    for (auto& node : nodes) {
        std::erase(node.inputs, id);
    }
    if (root == id) root = nodes.empty() ? 0 : nodes.front().id;
    return true;
}

bool ConditionGraph::wouldCycle(int parentId, int childId) const {
    if (parentId == childId) return true;

    std::vector<int> stack { childId };
    std::vector<int> seen;
    seen.reserve(nodes.size());

    while (!stack.empty()) {
        int current = stack.back();
        stack.pop_back();
        if (current == parentId) return true;
        if (std::ranges::find(seen, current) != seen.end()) continue;
        seen.push_back(current);

        auto const* node = find(current);
        if (!node) continue;
        for (int input : node->inputs) {
            stack.push_back(input);
        }
    }
    return false;
}

bool ConditionGraph::connect(int parentId, int childId) {
    auto* parent = find(parentId);
    if (!parent || !find(childId)) return false;
    if (!isLogic(parent->kind)) return false;
    if (std::ranges::find(parent->inputs, childId) != parent->inputs.end()) return false;
    if (static_cast<int>(parent->inputs.size()) >= maxArity(parent->kind)) return false;
    if (wouldCycle(parentId, childId)) return false;

    for (auto& node : nodes) {
        if (node.id == parentId) continue;
        std::erase(node.inputs, childId);
    }

    parent->inputs.push_back(childId);
    if (root == childId) root = parentId;
    resetRuntime();
    return true;
}

bool ConditionGraph::connectAt(int parentId, int childId, int index) {
    auto* parent = find(parentId);
    if (!parent || !find(childId)) return false;
    if (!isLogic(parent->kind)) return false;
    if (wouldCycle(parentId, childId)) return false;

    bool alreadyChild = std::ranges::find(parent->inputs, childId) != parent->inputs.end();
    if (!alreadyChild && static_cast<int>(parent->inputs.size()) >= maxArity(parent->kind)) return false;

    for (auto& node : nodes) {
        if (node.id == parentId) continue;
        std::erase(node.inputs, childId);
    }
    std::erase(parent->inputs, childId);

    int count = static_cast<int>(parent->inputs.size());
    if (index < 0 || index > count) index = count;
    parent->inputs.insert(parent->inputs.begin() + index, childId);

    if (root == childId) root = parentId;
    resetRuntime();
    return true;
}

int ConditionGraph::parentOf(int childId) const {
    for (auto const& node : nodes) {
        if (std::ranges::find(node.inputs, childId) != node.inputs.end()) return node.id;
    }
    return 0;
}

std::vector<int> ConditionGraph::topLevelIds() const {
    std::vector<int> result;
    result.reserve(nodes.size());
    for (auto const& node : nodes) {
        if (parentOf(node.id) == 0) result.push_back(node.id);
    }
    return result;
}

bool ConditionGraph::disconnect(int parentId, int childId) {
    auto* parent = find(parentId);
    if (!parent) return false;
    size_t before = parent->inputs.size();
    std::erase(parent->inputs, childId);
    bool changed = parent->inputs.size() != before;
    if (changed) resetRuntime();
    return changed;
}

void ConditionGraph::buildIndexCache() {
    evalIndex.clear();
    evalIndex.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); i++) {
        evalIndex.emplace_back(nodes[i].id, static_cast<int>(i));
    }
    std::ranges::sort(evalIndex, {}, &std::pair<int, int>::first);
    indexDirty = false;
}

int ConditionGraph::cachedIndexOf(int id) const {
    auto it = std::ranges::lower_bound(evalIndex, id, {}, &std::pair<int, int>::first);
    if (it == evalIndex.end() || it->first != id) return -1;
    return it->second;
}

void ConditionGraph::breakCycles() {
    std::vector<uint8_t> state(nodes.size(), 0);
    std::vector<int> stack;

    buildIndexCache();

    for (size_t start = 0; start < nodes.size(); start++) {
        if (state[start] != 0) continue;

        stack.clear();
        stack.push_back(static_cast<int>(start));

        while (!stack.empty()) {
            int index = stack.back();

            if (state[index] == 0) {
                state[index] = 1;

                auto& node = nodes[index];
                std::vector<int> kept;
                kept.reserve(node.inputs.size());
                for (int input : node.inputs) {
                    int childIndex = cachedIndexOf(input);
                    if (childIndex < 0) continue;
                    if (state[childIndex] == 1) continue;
                    kept.push_back(input);
                }
                node.inputs = std::move(kept);

                for (int input : node.inputs) {
                    int childIndex = cachedIndexOf(input);
                    if (childIndex >= 0 && state[childIndex] == 0) stack.push_back(childIndex);
                }
                continue;
            }

            if (state[index] == 1) state[index] = 2;
            stack.pop_back();
        }
    }
}

void ConditionGraph::sanitize() {
    indexDirty = true;
    if (nodes.size() > static_cast<size_t>(maxNodes)) nodes.resize(maxNodes);

    std::vector<int> ids;
    ids.reserve(nodes.size());
    for (auto it = nodes.begin(); it != nodes.end();) {
        if (it->kind == CondKind::Invalid || it->kind >= CondKind::Count || it->id <= 0 ||
            std::ranges::find(ids, it->id) != ids.end()) {
            it = nodes.erase(it);
            continue;
        }
        ids.push_back(it->id);
        ++it;
    }

    for (auto& node : nodes) {
        nextId = std::max(nextId, node.id + 1);

        node.threshold = std::clamp(node.threshold, -maxThreshold, maxThreshold);
        node.range = std::clamp(node.range, 0.f, maxRange);
        node.windowMs = std::clamp(node.windowMs, minWindowMs, maxWindowMs);
        node.waitMs = std::clamp(node.waitMs, 0.f, maxWaitMs);
        node.count = std::clamp(node.count, 1, maxEnemyCount);
        if (node.compare >= CondCompare::Count) node.compare = CondCompare::GreaterEqual;
        if (node.itemId.size() > 128) node.itemId.resize(128);
        if (!std::isfinite(node.x)) node.x = 0.f;
        if (!std::isfinite(node.y)) node.y = 0.f;

        if (!isLogic(node.kind)) {
            node.inputs.clear();
            continue;
        }

        std::vector<int> cleaned;
        for (int input : node.inputs) {
            if (input == node.id) continue;
            if (findIndex(input) < 0) continue;
            if (std::ranges::find(cleaned, input) != cleaned.end()) continue;
            if (static_cast<int>(cleaned.size()) >= maxArity(node.kind)) break;
            cleaned.push_back(input);
        }
        node.inputs = std::move(cleaned);
    }

    std::vector<int> claimed;
    claimed.reserve(nodes.size());
    for (auto& node : nodes) {
        std::vector<int> kept;
        for (int input : node.inputs) {
            if (std::ranges::find(claimed, input) != claimed.end()) continue;
            claimed.push_back(input);
            kept.push_back(input);
        }
        node.inputs = std::move(kept);
    }

    breakCycles();

    if (findIndex(root) < 0) root = nodes.empty() ? 0 : nodes.front().id;
}

bool ConditionGraph::evaluate(CondEvalContext& ctx, ConditionRuntime& rt) {
    if (indexDirty) buildIndexCache();

    int rootIndex = cachedIndexOf(root);
    if (rootIndex < 0) {
        resetRuntime();
        return false;
    }
    if (!ctx.getPlayer()) {
        resetRuntime();
        return false;
    }

    size_t count = nodes.size();
    evalState.assign(count, 0);
    evalValue.assign(count, 0);
    evalStack.clear();
    evalStack.push_back(rootIndex);

    while (!evalStack.empty()) {
        int index = evalStack.back();
        auto& node = nodes[index];

        if (evalState[index] == 2) {
            evalStack.pop_back();
            continue;
        }

        if (evalState[index] == 0) {
            evalState[index] = 1;

            if (isLogic(node.kind)) {
                bool pushed = false;
                for (int input : node.inputs) {
                    int childIndex = cachedIndexOf(input);
                    if (childIndex < 0) continue;
                    if (evalState[childIndex] != 0) continue;
                    evalStack.push_back(childIndex);
                    pushed = true;
                }
                if (pushed) continue;
            }
        }

        bool result = false;
        switch (node.kind) {
        case CondKind::And: {
            result = !node.inputs.empty();
            for (int input : node.inputs) {
                int childIndex = cachedIndexOf(input);
                if (childIndex < 0 || !evalValue[childIndex]) {
                    result = false;
                    break;
                }
            }
            break;
        }
        case CondKind::Or: {
            for (int input : node.inputs) {
                int childIndex = cachedIndexOf(input);
                if (childIndex >= 0 && evalValue[childIndex]) {
                    result = true;
                    break;
                }
            }
            break;
        }
        case CondKind::Not: {
            if (!node.inputs.empty()) {
                int childIndex = cachedIndexOf(node.inputs.front());
                result = childIndex >= 0 && !evalValue[childIndex];
            }
            break;
        }
        case CondKind::Wait: {
            int childIndex = node.inputs.empty() ? -1 : cachedIndexOf(node.inputs.front());
            bool child = childIndex >= 0 && evalValue[childIndex] != 0;
            auto& state = waitStates[node.id];
            auto now = std::chrono::steady_clock::now();
            if (child) {
                state.started = now;
                state.active = true;
                result = true;
            } else if (state.active) {
                float holdMs = std::clamp(node.waitMs, 0.f, maxWaitMs);
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.started).count();
                if (elapsed >= static_cast<int64_t>(holdMs)) {
                    waitStates.erase(node.id);
                    result = false;
                } else {
                    result = true;
                }
            } else {
                result = false;
            }
            break;
        }
        case CondKind::HoldingItem: {
            result = !node.itemId.empty() && ctx.heldItemId() == node.itemId;
            break;
        }
        case CondKind::SwitchToItem: {
            if (node.itemId.empty()) { result = false; break; }
            auto* plr = ctx.getPlayer();
            if (!plr || !plr->supplies || !plr->supplies->inventory) { result = false; break; }
            int sel = plr->supplies->selectedSlot;
            if (sel < 0 || sel >= 9) { result = false; break; }
            auto* inv = plr->supplies->inventory;
            auto* held = inv->getItem(sel);
            if (held && held->getItem()) {
                if (held->getItem()->namespacedId.getString() == node.itemId) {
                    result = true; break;
                }
            }
            for (int i = 0; i < 9; i++) {
                if (i == sel) continue;
                auto* stack = inv->getItem(i);
                if (!stack || !stack->getItem()) continue;
                if (stack->getItem()->namespacedId.getString() == node.itemId) {
                    plr->supplies->selectedSlot = i;
                    result = true;
                    break;
                }
            }
            break;
        }
        case CondKind::Health: {
            auto value = ctx.health();
            if (value) {
                float compareAgainst = *value;
                if (node.percent) {
                    auto maxValue = ctx.maxHealth();
                    if (maxValue && *maxValue > 0.f) compareAgainst = (*value / *maxValue) * 100.f;
                    else compareAgainst = 0.f;
                }
                result = compareValue(node.compare, compareAgainst, node.threshold);
            }
            break;
        }
        case CondKind::Hunger: {
            auto value = ctx.hunger();
            if (value) result = compareValue(node.compare, *value, node.threshold);
            break;
        }
        case CondKind::FallDistance: {
            auto value = ctx.fallDistance();
            if (value) result = compareValue(node.compare, *value, node.threshold);
            break;
        }
        case CondKind::RiseDistance: {
            result = compareValue(node.compare, rt.riseDistance(), node.threshold);
            break;
        }
        case CondKind::TookDamage:
            result = rt.tookDamageAgeMs() <= node.windowMs;
            break;
        case CondKind::DealtDamage:
            result = rt.dealtDamageAgeMs() <= node.windowMs;
            break;
        case CondKind::EnemiesInRange:
            result = ctx.enemiesWithin(node.range) >= node.count;
            break;
        case CondKind::Sprinting:
            result = ctx.sprinting();
            break;
        case CondKind::Walking:
            result = ctx.walking();
            break;
        case CondKind::Sneaking:
            result = ctx.sneaking();
            break;
        case CondKind::OnGround:
            result = ctx.onGround();
            break;
        default:
            result = false;
            break;
        }

        if (node.invert) result = !result;

        evalValue[index] = result ? 1 : 0;
        evalState[index] = 2;
        evalStack.pop_back();
    }

    return evalValue[rootIndex] != 0;
}

nlohmann::json ConditionGraph::toJson() const {
    nlohmann::json out;
    out["root"] = root;
    out["nodes"] = nlohmann::json::array();

    for (auto const& node : nodes) {
        nlohmann::json n;
        n["id"] = node.id;
        n["kind"] = kindKey(node.kind);
        n["x"] = node.x;
        n["y"] = node.y;
        n["invert"] = node.invert;

        if (isLogic(node.kind)) {
            n["inputs"] = node.inputs;
        }
        if (usesThreshold(node.kind)) {
            n["compare"] = static_cast<int>(node.compare);
            n["threshold"] = node.threshold;
        }
        if (node.kind == CondKind::Health) n["percent"] = node.percent;
        if (node.kind == CondKind::HoldingItem) n["itemId"] = node.itemId;
        if (node.kind == CondKind::Wait) n["waitMs"] = node.waitMs;
        if (node.kind == CondKind::EnemiesInRange) {
            n["range"] = node.range;
            n["count"] = node.count;
        }
        if (node.kind == CondKind::TookDamage || node.kind == CondKind::DealtDamage) {
            n["windowMs"] = node.windowMs;
        }

        out["nodes"].push_back(std::move(n));
    }

    return out;
}

void ConditionGraph::fromJson(nlohmann::json const& j) {
    nodes.clear();
    waitStates.clear();
    root = 0;
    nextId = 1;
    indexDirty = true;

    if (!j.is_object()) return;
    if (!j.contains("nodes") || !j["nodes"].is_array()) return;

    for (auto const& n : j["nodes"]) {
        if (!n.is_object()) continue;
        if (nodes.size() >= static_cast<size_t>(maxNodes)) break;

        CondNode node;
        node.id = n.contains("id") && n["id"].is_number_integer() ? n["id"].get<int>() : 0;
        if (node.id <= 0) continue;

        std::string key = n.contains("kind") && n["kind"].is_string() ? n["kind"].get<std::string>() : "";
        node.kind = kindFromKey(key);
        if (node.kind == CondKind::Invalid) continue;

        node.x = n.contains("x") && n["x"].is_number() ? n["x"].get<float>() : 0.f;
        node.y = n.contains("y") && n["y"].is_number() ? n["y"].get<float>() : 0.f;
        node.invert = n.contains("invert") && n["invert"].is_boolean() ? n["invert"].get<bool>() : false;

        if (n.contains("inputs") && n["inputs"].is_array()) {
            for (auto const& input : n["inputs"]) {
                if (input.is_number_integer()) node.inputs.push_back(input.get<int>());
            }
        }
        if (n.contains("compare") && n["compare"].is_number_integer()) {
            int cmp = n["compare"].get<int>();
            if (cmp >= 0 && cmp < static_cast<int>(CondCompare::Count)) node.compare = static_cast<CondCompare>(cmp);
        }
        if (n.contains("threshold") && n["threshold"].is_number()) node.threshold = n["threshold"].get<float>();
        if (n.contains("percent") && n["percent"].is_boolean()) node.percent = n["percent"].get<bool>();
        if (n.contains("itemId") && n["itemId"].is_string()) node.itemId = n["itemId"].get<std::string>();
        if (n.contains("waitMs") && n["waitMs"].is_number()) node.waitMs = n["waitMs"].get<float>();
        if (n.contains("range") && n["range"].is_number()) node.range = n["range"].get<float>();
        if (n.contains("count") && n["count"].is_number_integer()) node.count = n["count"].get<int>();
        if (n.contains("windowMs") && n["windowMs"].is_number()) node.windowMs = n["windowMs"].get<float>();

        nodes.push_back(std::move(node));
    }

    root = j.contains("root") && j["root"].is_number_integer() ? j["root"].get<int>() : 0;
    sanitize();
}
