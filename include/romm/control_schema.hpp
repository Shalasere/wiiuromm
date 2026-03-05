#pragma once

#include "romm/app_core.hpp"

namespace romm {

enum class ControlProfile {
    WiiU = 0,
    WiiRemote,
    GameCube
};

enum class LogicalButton {
    None = 0,
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Back,
    Queue,
    StartWork,
    Search,
    Diagnostics,
    Updater,
    Quit
};

Action mapButtonToAction(ControlProfile profile, LogicalButton button);

} // namespace romm
