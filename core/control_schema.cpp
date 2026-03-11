#include "romm/control_schema.hpp"

namespace romm {

Action mapButtonToAction(ControlProfile profile, LogicalButton button) {
    switch (button) {
    case LogicalButton::Up:
        return Action::Up;
    case LogicalButton::Down:
        return Action::Down;
    case LogicalButton::Left:
        return Action::Left;
    case LogicalButton::Right:
        return Action::Right;
    case LogicalButton::Confirm:
        return Action::Select;
    case LogicalButton::Back:
        return Action::Back;
    case LogicalButton::Queue:
        return Action::OpenQueue;
    case LogicalButton::StartWork:
        return Action::StartDownload;
    case LogicalButton::Search:
        return Action::OpenSearch;
    case LogicalButton::Quit:
        return Action::Quit;
    case LogicalButton::Diagnostics:
        if (profile == ControlProfile::WiiRemote)
            return Action::None;
        return Action::OpenDiagnostics;
    case LogicalButton::Updater:
        if (profile == ControlProfile::WiiRemote)
            return Action::None;
        return Action::OpenUpdater;
    case LogicalButton::None:
    default:
        return Action::None;
    }
}

} // namespace romm
