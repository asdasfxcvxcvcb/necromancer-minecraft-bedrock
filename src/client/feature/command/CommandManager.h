#pragma once
#include "Command.h"

class CommandManager final : public Manager<ICommand> {
public:
    std::string prefix = ".";

    CommandManager();
    virtual ~CommandManager() = default;

    bool runCommand(std::string const& line);
    void refreshLocalization();
};
