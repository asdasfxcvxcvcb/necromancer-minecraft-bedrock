#pragma once
#include <functional>
#include <tuple>
#include <type_traits>

template<typename Base, typename... Items>
class StaticManager {
protected:
    std::tuple<Items...> items = { Items()... };
    std::vector<std::shared_ptr<Base>> dynamicItems;

public:
    StaticManager()
        : items() {}
    StaticManager(StaticManager<Base, Items...>&) = delete;
    StaticManager(StaticManager<Base, Items...>&&) = delete;

    template<typename F>
    void forEach(F const& func) {
        forEachImpl(func, items);
        for (auto& item : dynamicItems) {
            func(*item);
        }
    }

    template<typename T>
    T& get() {
        return std::get<T>(items);
    }

    virtual ~StaticManager() = default;

private:
    template<typename F>
    void forEachImpl(F const&, std::tuple<>&) {}

    template<typename F, typename... Ts>
    void forEachImpl(F const& fn, std::tuple<Ts...>& list) {
        fn(list._Myfirst._Val);
        forEachImpl(fn, list._Get_rest());
    }
};
