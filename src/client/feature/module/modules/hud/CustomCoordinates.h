#pragma once
#include "../../TextModule.h"

class CustomCoordinates : public TextModule {
public:
    CustomCoordinates();

    void onInit() override;

    std::wstringstream text(bool isDefault, bool inEditor) override;

private:
    ValueType showDimension = BoolValue(false);
};
