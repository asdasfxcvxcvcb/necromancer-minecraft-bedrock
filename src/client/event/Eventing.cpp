#include "pch.h"
#include "Eventing.h"
#include "client/Necromancer.h"

Eventing& Eventing::get() {
    return Necromancer::getEventing();
}
