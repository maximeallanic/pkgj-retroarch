#include "doctest.h"
#include "config.hpp"
#include "systems.hpp"

TEST_CASE("config url falls back to the table default when unset")
{
    Config c{};
    // gb has a default item; psp does not.
    CHECK(pkgi_config_url(c, 0) == pkgi_system_by_id("gb")->default_item);
    CHECK(pkgi_config_url(c, 7) == "");
}

TEST_CASE("an explicit system_urls entry overrides the default")
{
    Config c{};
    c.system_urls["gb"] = "my-custom-item";
    CHECK(pkgi_config_url(c, 0) == "my-custom-item");
}
