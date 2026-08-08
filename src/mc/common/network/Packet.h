#pragma once
namespace SDK {
    enum class PacketID : uint8_t {
        NONE = 0,
        LOGIN = 0x1,
        TEXT = 0x9,
        ADD_PLAYER = 0xC,
        MOVE_ACTOR_ABSOLUTE = 0x12,
        MOVE_PLAYER = 0x13,
        INTERACT = 0x21,
        ACTOR_EVENT = 0x1B,
        SET_ENTITY_DATA = 0x27,
        SET_ENTITY_MOTION = 0x28,
        CHANGE_DIMENSION = 0x3D,
        TRANSFER = 0x55,
        SET_TITLE = 0x58,
        COMMAND_REQUEST = 0x4D,
        MODAL_FORM_REQUEST = 0x64,
        SET_SCORE = 0x6c,
        MOVE_ACTOR_DELTA = 0x6F,
        NETWORK_STACK_LATENCY = 0x73,
        PLAYER_AUTH_INPUT = 0x90,
        TOAST_REQUEST = 0xBA,
        COUNT,
    };

    class Packet {
    public:
        int32_t priority = 2;
        int32_t reliability = 1;
        uint8_t subClientId = 0;
        bool isHandled = false;
        void* unknown = nullptr;
        void*** handler = nullptr;
        int32_t compressibility = 0;

    public:
        virtual ~Packet() {};
        virtual PacketID getID() { return PacketID::NONE; };
        virtual std::string getName() { return ""; };
        virtual void write(void* stream) {};
        virtual void readExtended(void* stream) {};
        virtual bool allowBatch() { return false; };
        virtual void _read(void* stream) {};
    };
}
