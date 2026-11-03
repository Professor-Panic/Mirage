#pragma once
#include <string>

#include "rui.h"
#include "mirage_core/runtime.h"

// The console panel: shows the script log and has the "clear" button.
class ConsoleUi
{
public:
    ConsoleUi(rui::Context& ui, MirageCoreRuntime& core) : core_(core)
    {
        ui.runtime.Bind("console_text", text_);
        ui.runtime.Function("clearLog", [this]() { core_.scriptLog.Clear(); });
    }

    ConsoleUi(const ConsoleUi&) = delete;
    ConsoleUi& operator=(const ConsoleUi&) = delete;

    // Once per frame.
    void Update()
    {
        if (core_.scriptLog.ConsumeDirty()) text_ = core_.scriptLog.Text();
    }

private:
    MirageCoreRuntime& core_;
    std::string        text_;   // the UI is bound to this
};
