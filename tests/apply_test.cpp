#include "apply.h"

#include <string>

#include <gtest/gtest.h>

#include "project.h"

namespace {

wirebard::NetworkPaths paths_for(const std::string& name) {
    return wirebard::NetworkPaths{.name = name,
                                  .dir = "/etc/wireguard/partials/" + name,
                                  .output = "/etc/wireguard/" + name + ".conf",
                                  .server_key = "/etc/wireguard/partials/" + name + "/server.key"};
}

TEST(ApplyTest, PlanCarriesConfPathContentsAndInterface) {
    auto plan = wirebard::plan_apply(paths_for("backups"), "SECRET-CONF-BYTES");
    EXPECT_EQ(plan.interface, "backups");
    EXPECT_EQ(plan.conf_path, "/etc/wireguard/backups.conf");
    EXPECT_EQ(plan.conf_contents, "SECRET-CONF-BYTES");
}

TEST(ApplyTest, DescribePlanNamesTheStepsAndHidesTheSecret) {
    auto plan = wirebard::plan_apply(paths_for("backups"), "PrivateKey = TOPSECRET");
    std::string desc = wirebard::describe_plan(plan);

    // Mentions each step against the right unit/interface...
    EXPECT_NE(desc.find("systemctl enable wg-quick@backups"), std::string::npos);
    EXPECT_NE(desc.find("wg syncconf backups"), std::string::npos);
    EXPECT_NE(desc.find("systemctl start wg-quick@backups"), std::string::npos);
    // ...but NEVER leaks the conf contents (which carry the private key).
    EXPECT_EQ(desc.find("TOPSECRET"), std::string::npos);
}

} // namespace
