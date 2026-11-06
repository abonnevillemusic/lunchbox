#pragma once

#include "Module500Interface.h"
#include "modules/VP600Module500.h"
#include <NV73Module500.h>
#include <NV73EQModule500.h>

#include <functional>
#include <memory>
#include <vector>

namespace sdk500
{

struct ModuleFactoryEntry
{
    ModuleDescriptor descriptor;
    std::function<std::unique_ptr<Module500>()> createInstance;
};

/**
 * Central Registry of all 500-series modules built in the pluginz workspace.
 * Every new plugin we create registers one line here to appear automatically in every slot
 * of the LBX-500 Lunchbox!
 */
class ModuleRegistry
{
public:
    static const std::vector<ModuleFactoryEntry>& getAvailableModules()
    {
        static const std::vector<ModuleFactoryEntry> entries {
            {
                nv73::NV73Module500::staticDescriptor(),
                [] () -> std::unique_ptr<Module500> { return std::make_unique<nv73::NV73Module500>(); }
            },
            {
                nv73eq::NV73EQModule500::staticDescriptor(),
                [] () -> std::unique_ptr<Module500> { return std::make_unique<nv73eq::NV73EQModule500>(); }
            },
            {
                VP600Module500::staticDescriptor(),
                [] () -> std::unique_ptr<Module500> { return std::make_unique<VP600Module500>(); }
            }
        };
        return entries;
    }

    [[nodiscard]] static std::unique_ptr<Module500> createById (const juce::String& id)
    {
        for (const auto& entry : getAvailableModules())
            if (entry.descriptor.id == id)
                return entry.createInstance();
        return nullptr;
    }
};

} // namespace sdk500
