#pragma once
class NecromancerClientMessageEvent : public Event {
public:
    static const uint32_t hash = TOHASH(NecromancerClientMessageEvent);

    NecromancerClientMessageEvent(std::string const& msg)
        : message(msg) {}

    [[nodiscard]] std::string getMessage() { return message; }

private:
    std::string message;
};
