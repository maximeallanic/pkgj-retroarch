#include "config.hpp"
#include "systems.hpp"

std::string pkgi_config_url(const Config& config, int mode)
{
    const SystemDef& sys = pkgi_system(mode);
    auto it = config.system_urls.find(sys.id);
    if (it != config.system_urls.end() && !it->second.empty())
        return it->second;
    return sys.default_item;
}
