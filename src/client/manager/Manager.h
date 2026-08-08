#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

template<typename T>
class Manager {
public:
    Manager() = default;
    Manager(Manager&) = delete;
    Manager(Manager&&) = delete;

    virtual ~Manager() = default;

    template<typename F>
    void forEach(F&& callback) {
        for (auto& it : items) {
            callback(it);
        }
    }

    void erase(std::shared_ptr<T> item) {
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (*it == item) {
                items.erase(it);
                return;
            }
        }
    }

protected:
    std::mutex mutex;
    std::vector<std::shared_ptr<T>> items = {};
};
