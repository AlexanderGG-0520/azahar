// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <catch2/catch_test_macros.hpp>
#include "core/hle/service/sm/process_service_redirect.h"

TEST_CASE("service redirects are isolated by process and opt-in", "[core][service][sm]") {
    Service::SM::ProcessServiceRedirect redirects;

    REQUIRE_FALSE(redirects.Resolve(1, "nwm::UDS"));
    redirects.SetLevel256(1, true, false);
    REQUIRE(redirects.Resolve(1, "nwm::UDS") == "plg:UDS");
    REQUIRE_FALSE(redirects.Resolve(2, "nwm::UDS"));
    REQUIRE_FALSE(redirects.Resolve(1, "dlp:SRVR"));
    REQUIRE_FALSE(redirects.Resolve(1, "dlp:CLNT"));

    redirects.SetLevel256(1, true, true);
    REQUIRE(redirects.Resolve(1, "dlp:SRVR") == "plg:DLP");
    REQUIRE(redirects.Resolve(1, "dlp:FKCL") == "plg:FKCL");
    REQUIRE(redirects.Resolve(1, "dlp:CLNT") == "plg:FKCL");
    REQUIRE_FALSE(redirects.Resolve(1, "soc:U"));

    redirects.OnProcessExit(1);
    REQUIRE_FALSE(redirects.Resolve(1, "nwm::UDS"));
    redirects.SetLevel256(1, true, true);
    REQUIRE(redirects.Resolve(1, "nwm::UDS") == "plg:UDS");
    redirects.SetLevel256(1, false, false);
    REQUIRE_FALSE(redirects.Resolve(1, "nwm::UDS"));
}

TEST_CASE("Level256 UDS IPC shape translations match the public Luma hook", "[core][service][sm]") {
    using Service::SM::TranslateLevel256UdsCommand;
    using Service::SM::UdsCommandShape;

    REQUIRE((TranslateLevel256UdsCommand(14, 0, 6) == UdsCommandShape{31, 7, 0}));
    REQUIRE((TranslateLevel256UdsCommand(31, 0, 6) == UdsCommandShape{31, 7, 0}));
    REQUIRE((TranslateLevel256UdsCommand(15, 16, 4) == UdsCommandShape{15, 20, 0}));
    REQUIRE((TranslateLevel256UdsCommand(16, 1, 2) == UdsCommandShape{16, 3, 0}));
    REQUIRE((TranslateLevel256UdsCommand(17, 1, 0) == UdsCommandShape{17, 2, 0}));
    REQUIRE((TranslateLevel256UdsCommand(20, 3, 0) == UdsCommandShape{20, 4, 0}));
    REQUIRE((TranslateLevel256UdsCommand(23, 6, 2) == UdsCommandShape{23, 8, 0}));
    REQUIRE((TranslateLevel256UdsCommand(27, 12, 2) == UdsCommandShape{27, 14, 0}));
    REQUIRE((TranslateLevel256UdsCommand(29, 1, 4) == UdsCommandShape{29, 5, 0}));
    REQUIRE((TranslateLevel256UdsCommand(30, 2, 4) == UdsCommandShape{30, 6, 0}));
    REQUIRE_FALSE(TranslateLevel256UdsCommand(23, 6, 1));
    REQUIRE_FALSE(TranslateLevel256UdsCommand(99, 0, 0));
}

TEST_CASE("PLGUDS control state is owner-scoped and pause/DLP-aware",
          "[core][service][sm][level256]") {
    Service::SM::ProcessServiceRedirect redirects;

    auto result = redirects.ProcessPlgUdsControl(16, 100, 2, 0, 15, 1);
    REQUIRE(result.handled);
    REQUIRE_FALSE(result.forward_to_server);
    REQUIRE(result.response_value == 0xD90007EA);
    REQUIRE_FALSE(redirects.Resolve(16, "nwm::UDS"));

    result = redirects.ProcessPlgUdsControl(16, 100, 2, 0, 16, 0);
    REQUIRE(result.handled);
    REQUIRE(result.forward_to_server);
    REQUIRE(redirects.Resolve(16, "nwm::UDS") == "plg:UDS");
    REQUIRE(redirects.GetState(16));
    REQUIRE_FALSE(redirects.GetState(16)->is_log_mode);
    REQUIRE_FALSE(redirects.GetState(16)->is_paused);
    REQUIRE_FALSE(redirects.Resolve(17, "nwm::UDS"));

    result = redirects.ProcessPlgUdsControl(17, 100, 2, 0, 17, 1);
    REQUIRE(result.handled);
    REQUIRE(result.forward_to_server);
    REQUIRE(redirects.GetState(17));
    REQUIRE(redirects.GetState(17)->is_log_mode);
    REQUIRE_FALSE(redirects.IsConnectedProcess(17));

    result = redirects.ProcessPlgUdsControl(18, 101, 0, 0, 0, 0);
    REQUIRE(result.handled);
    REQUIRE_FALSE(result.forward_to_server);
    REQUIRE(result.response_value == 0xD90007EA);
    REQUIRE(redirects.Resolve(16, "nwm::UDS") == "plg:UDS");

    result = redirects.ProcessPlgUdsControl(16, 104, 1, 0, 1, 0);
    REQUIRE(result.handled);
    REQUIRE_FALSE(result.forward_to_server);
    REQUIRE(redirects.SignalOnAttach(16));

    result = redirects.ProcessPlgUdsControl(16, 105, 1, 0, 1, 0);
    REQUIRE(result.handled);
    REQUIRE(redirects.IsPaused(16));
    REQUIRE_FALSE(redirects.Resolve(16, "nwm::UDS"));
    REQUIRE_FALSE(redirects.Resolve(16, "dlp:SRVR"));

    result = redirects.ProcessPlgUdsControl(16, 105, 1, 0, 0, 0);
    REQUIRE(result.response_value == 0);
    REQUIRE_FALSE(redirects.IsPaused(16));
    result = redirects.ProcessPlgUdsControl(16, 106, 1, 0, 2, 0);
    REQUIRE(result.response_value == 0xD90007FD);
    REQUIRE_FALSE(redirects.Resolve(16, "dlp:SRVR"));
    result = redirects.ProcessPlgUdsControl(16, 106, 1, 0, 1, 0);
    REQUIRE(result.response_value == 0);
    REQUIRE(redirects.Resolve(16, "dlp:SRVR") == "plg:DLP");

    result = redirects.ProcessPlgUdsControl(16, 101, 0, 0, 0, 0);
    REQUIRE(result.handled);
    REQUIRE(result.forward_to_server);
    REQUIRE_FALSE(redirects.Resolve(16, "nwm::UDS"));
    REQUIRE_FALSE(redirects.SignalOnAttach(16));

    // PID reuse starts without the previous process's redirect or pause state.
    REQUIRE_FALSE(redirects.IsPaused(16));
    result = redirects.ProcessPlgUdsControl(16, 105, 1, 0, 1, 0);
    REQUIRE(result.response_value == 0xD90007EA);
}
