// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <cstring>
#include <stdexcept>
#include <catch2/catch_test_macros.hpp>
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/ipc.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/level256_start_scan_diagnostic.h"
#include "core/hle/kernel/luma3ds/compatibility.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/kernel/server_port.h"
#include "core/hle/kernel/server_session.h"
#include "core/hle/kernel/session.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/sm/srv.h"
#include "core/memory.h"

namespace {

class MockNwmUds final : public Service::ServiceFramework<MockNwmUds> {
public:
    MockNwmUds() : ServiceFramework("nwm::UDS") {
        static const FunctionInfo functions[] = {{0x55, &MockNwmUds::Echo, "Echo"}};
        RegisterHandlers(functions);
    }

private:
    void Echo(Kernel::HLERequestContext& ctx) {
        IPC::RequestParser rp(ctx);
        const u32 value = rp.Pop<u32>();
        IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
        rb.Push(ResultSuccess);
        rb.Push(value + 1);
    }
};

struct TestEnvironment {
    Core::Timing timing{1, 100};
    Core::System system;
    Memory::MemorySystem memory{system};
    Kernel::KernelSystem kernel{memory, timing, [] {}, Kernel::MemoryMode::NewProd, 1};
    Service::SM::ServiceManager manager{system, kernel};

    std::shared_ptr<Kernel::Process> MakeProcess() {
        auto process = kernel.CreateProcess(kernel.CreateCodeSet("test", 0));
        process->resource_limit =
            kernel.ResourceLimit().GetForCategory(Kernel::ResourceLimitCategory::Application);
        process->status = Kernel::ProcessStatus::Running;
        auto code_mem = std::make_shared<BufferMem>(Memory::CITRA_PAGE_SIZE);
        const MemoryRef code_ref{code_mem};
        if (process->vm_manager
                .MapBackingMemory(0x00100000, code_ref, Memory::CITRA_PAGE_SIZE,
                                  Kernel::MemoryState::Code)
                .Failed()) {
            throw std::runtime_error("failed to map mock process code");
        }
        return process;
    }

    std::shared_ptr<Kernel::Thread> MakeThread(const std::shared_ptr<Kernel::Process>& process) {
        auto thread =
            kernel.CreateThread("ipc-test", 0x00100000, 0x30, 0, 0, 0x08000000, process, false);
        if (thread.Failed()) {
            throw std::runtime_error("failed to create mock thread");
        }
        auto thread_ptr = thread.Unwrap();
        thread_ptr->status = Kernel::ThreadStatus::Running;
        return thread_ptr;
    }

    void Send(const std::shared_ptr<Kernel::Process>& process,
              const std::shared_ptr<Kernel::Thread>& thread,
              const std::shared_ptr<Kernel::ClientSession>& session,
              const std::array<u32, IPC::COMMAND_BUFFER_LENGTH>& request) {
        memory.WriteBlock(*process, thread->GetCommandBufferAddress(), request.data(),
                          request.size() * sizeof(u32));
        thread->status = Kernel::ThreadStatus::Running;
        REQUIRE(session->SendSyncRequest(thread) == ResultSuccess);
    }

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> ReadCommand(
        const std::shared_ptr<Kernel::Process>& process,
        const std::shared_ptr<Kernel::Thread>& thread) {
        std::array<u32, IPC::COMMAND_BUFFER_LENGTH> result{};
        memory.ReadBlock(*process, thread->GetCommandBufferAddress(), result.data(),
                         result.size() * sizeof(u32));
        return result;
    }

    void WriteCommand(const std::shared_ptr<Kernel::Process>& process,
                      const std::shared_ptr<Kernel::Thread>& thread,
                      const std::array<u32, IPC::COMMAND_BUFFER_LENGTH>& command) {
        memory.WriteBlock(*process, thread->GetCommandBufferAddress(), command.data(),
                          command.size() * sizeof(u32));
    }

    std::shared_ptr<Kernel::ClientSession> ConnectSrv() {
        std::shared_ptr<Kernel::ClientSession> session;
        if (manager.ConnectToService(&session, "srv:") != ResultSuccess) {
            throw std::runtime_error("failed to connect to srv:");
        }
        return session;
    }

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> GetService(
        const std::shared_ptr<Kernel::Process>& process,
        const std::shared_ptr<Kernel::Thread>& thread,
        const std::shared_ptr<Kernel::ClientSession>& srv_session, const std::string& service_name,
        u32 flags = 1) {
        std::array<u32, IPC::COMMAND_BUFFER_LENGTH> request{};
        request[0] = IPC::MakeHeader(5, 4, 0);
        std::memcpy(&request[1], service_name.data(), service_name.size());
        request[3] = static_cast<u32>(service_name.size());
        request[4] = flags;
        Send(process, thread, srv_session, request);
        return ReadCommand(process, thread);
    }

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> RegisterGuestService(
        const std::shared_ptr<Kernel::Process>& process,
        const std::shared_ptr<Kernel::Thread>& thread,
        const std::shared_ptr<Kernel::ClientSession>& srv_session, const std::string& service_name,
        u32 max_sessions = 8) {
        std::array<u32, IPC::COMMAND_BUFFER_LENGTH> request{};
        request[0] = IPC::MakeHeader(3, 4, 0);
        std::memcpy(&request[1], service_name.data(), service_name.size());
        request[3] = static_cast<u32>(service_name.size());
        request[4] = max_sessions;
        Send(process, thread, srv_session, request);
        return ReadCommand(process, thread);
    }
};

class TestServerCloseCallback final : public Kernel::WakeupCallback {
public:
    void WakeUp(Kernel::ThreadWakeupReason reason, std::shared_ptr<Kernel::Thread> thread,
                std::shared_ptr<Kernel::WaitObject> object) override {
        REQUIRE(reason == Kernel::ThreadWakeupReason::Signal);
        auto server = std::dynamic_pointer_cast<Kernel::ServerSession>(object);
        REQUIRE(server);
        const auto result = server->ReceiveRequest(thread);
        thread->SetWaitSynchronizationResult(result);
        thread->SetWaitSynchronizationOutput(thread->GetWaitObjectIndex(server.get()));
    }
};

} // namespace

TEST_CASE("Level256 StartScan beacon header snapshot reads only valid 12-byte ranges",
          "[core][service][sm][level256][diagnostic]") {
    const auto translated = Service::SM::TranslateLevel256UdsCommand(15, 16, 4);
    REQUIRE(translated);
    REQUIRE((translated == Service::SM::UdsCommandShape{15, 20, 0}));

    std::array<u32, 21> start_scan_request{};
    start_scan_request[0] = IPC::MakeHeader(15, 16, 4);
    start_scan_request[1] = 0x4000;
    start_scan_request[19] = IPC::MappedBufferDesc(0x4000, IPC::W);
    start_scan_request[20] = 0x00200000;
    const auto original_request = Kernel::DecodeLevel256StartScanBuffer(start_scan_request);
    start_scan_request[0] = IPC::MakeHeader(translated->command_id, translated->normal_params,
                                            translated->translate_params);
    REQUIRE(start_scan_request[0] == 0x000F0500);
    const auto forwarded_request = Kernel::DecodeLevel256StartScanBuffer(start_scan_request);
    REQUIRE(forwarded_request.status == Kernel::Level256StartScanBufferStatus::Valid);
    REQUIRE(forwarded_request.descriptor == original_request.descriptor);
    REQUIRE(forwarded_request.output_address == original_request.output_address);

    TestEnvironment env;
    auto process = env.MakeProcess();
    constexpr VAddr output_address = 0x00200000;
    auto output_memory = std::make_shared<BufferMem>(Memory::CITRA_PAGE_SIZE);
    REQUIRE(process->vm_manager
                .MapBackingMemory(output_address, MemoryRef{output_memory}, Memory::CITRA_PAGE_SIZE,
                                  Kernel::MemoryState::Shared)
                .Succeeded());

    constexpr std::array<u32, 3> expected = {0x4000, 0x78, 3};
    env.memory.WriteBlock(*process, output_address, expected.data(), sizeof(expected));

    Kernel::Level256StartScanBuffer request{};
    request.status = Kernel::Level256StartScanBufferStatus::Valid;
    request.request_max_size = 0x4000;
    request.mapped_size = 0x4000;
    request.output_address = output_address;
    const auto snapshot = Kernel::ReadLevel256BeaconReplySnapshot(env.memory, *process, request);
    REQUIRE(snapshot.status == Kernel::Level256StartScanBufferStatus::Valid);
    REQUIRE(snapshot.max_size == expected[0]);
    REQUIRE(snapshot.size == expected[1]);
    REQUIRE(snapshot.total_entries == expected[2]);

    std::array<u32, 3> after_read{};
    env.memory.ReadBlock(*process, output_address, after_read.data(), sizeof(after_read));
    REQUIRE(after_read == expected);

    request.output_address = 0x00300000;
    const auto invalid_pointer =
        Kernel::ReadLevel256BeaconReplySnapshot(env.memory, *process, request);
    REQUIRE(invalid_pointer.status == Kernel::Level256StartScanBufferStatus::UnreadableAddress);

    request.mapped_size = 8;
    const auto short_buffer =
        Kernel::ReadLevel256BeaconReplySnapshot(env.memory, *process, request);
    REQUIRE(short_buffer.status == Kernel::Level256StartScanBufferStatus::BufferTooSmall);
}

TEST_CASE("Level256 IPC classification uses the connected ClientPort name",
          "[core][service][sm][level256][integration]") {
    TestEnvironment env;

    const auto verify_port = [&env](const std::string& service_name,
                                    Kernel::Luma3DS::Level256IpcService expected) {
        auto [server_port, client_port] = env.kernel.CreatePortPair(4, service_name);
        std::shared_ptr<Kernel::ClientSession> session;
        REQUIRE(client_port->Connect(&session) == ResultSuccess);
        REQUIRE(server_port->GetName() == service_name + "_Server");
        REQUIRE(client_port->GetName() == service_name + "_Client");
        REQUIRE(session->GetName() == service_name + "_Server_Client");
        REQUIRE(session->parent);
        REQUIRE(session->parent->port == client_port);
        REQUIRE(Kernel::Luma3DS::ClassifyLevel256IpcPortName(session->GetName()) ==
                Kernel::Luma3DS::Level256IpcService::None);
        REQUIRE(Kernel::Luma3DS::ClassifyLevel256IpcPortName(client_port->GetName()) == expected);
    };

    verify_port("plg:UDS", Kernel::Luma3DS::Level256IpcService::PluginUds);
    verify_port("ndm:u", Kernel::Luma3DS::Level256IpcService::Ndm);
    verify_port("soc:U", Kernel::Luma3DS::Level256IpcService::Soc);

    auto [portless_server, portless_client] = env.kernel.CreateSessionPair("plg:UDS", nullptr);
    REQUIRE(portless_server);
    REQUIRE(portless_client->parent);
    REQUIRE_FALSE(portless_client->parent->port);
    REQUIRE(Kernel::Luma3DS::ClassifyLevel256IpcPortName({}) ==
            Kernel::Luma3DS::Level256IpcService::None);
    REQUIRE(Kernel::Luma3DS::ClassifyLevel256IpcPortName("other:service_Client") ==
            Kernel::Luma3DS::Level256IpcService::None);
}

TEST_CASE("LLE session close completes pending IPC and wakes server waiters",
          "[core][service][sm][integration][ipc]") {
    TestEnvironment env;
    auto process = env.MakeProcess();
    auto server_process = env.MakeProcess();
    auto client_thread = env.MakeThread(process);
    auto pending_thread = env.MakeThread(process);
    auto server_thread = env.MakeThread(server_process);
    auto pair = env.kernel.CreateSessionPair("disconnect-test", nullptr);
    auto server = std::move(pair.first);
    auto client = std::move(pair.second);

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> request{};
    request[0] = IPC::MakeHeader(0x55, 1, 0);
    request[1] = 1;
    env.Send(process, client_thread, client, request);
    request[1] = 2;
    env.Send(process, pending_thread, client, request);

    server->Acquire(server_thread.get());
    REQUIRE(server->currently_handling == pending_thread);
    REQUIRE(server->pending_requesting_threads.size() == 1);

    server_thread->status = Kernel::ThreadStatus::WaitSynchAny;
    server_thread->wait_objects.emplace_back(server);
    server_thread->wakeup_callback = std::make_shared<TestServerCloseCallback>();
    server->AddWaitingThread(server_thread);

    client.reset();

    REQUIRE(server->parent->client == nullptr);
    REQUIRE(server->currently_handling == nullptr);
    REQUIRE(server->pending_requesting_threads.empty());
    REQUIRE(client_thread->status == Kernel::ThreadStatus::Ready);
    REQUIRE(pending_thread->status == Kernel::ThreadStatus::Ready);
    REQUIRE(client_thread->context.cpu_registers[0] == Kernel::ResultSessionClosed.raw);
    REQUIRE(pending_thread->context.cpu_registers[0] == Kernel::ResultSessionClosed.raw);
    REQUIRE(server_thread->status == Kernel::ThreadStatus::Ready);
    REQUIRE(server_thread->context.cpu_registers[0] == Kernel::ResultSessionClosed.raw);
}

TEST_CASE("LLE malformed IPC translation returns an error and keeps session reusable",
          "[core][service][sm][integration][ipc]") {
    TestEnvironment env;
    auto process = env.MakeProcess();
    auto server_process = env.MakeProcess();
    auto client_thread = env.MakeThread(process);
    auto server_thread = env.MakeThread(server_process);
    auto pair = env.kernel.CreateSessionPair("translation-error-test", nullptr);
    auto server = std::move(pair.first);
    auto client = std::move(pair.second);

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> malformed{};
    malformed[0] = IPC::MakeHeader(0x55, 1, 1);
    malformed[1] = 0;
    malformed[2] = IPC::CopyHandleDesc(2); // Descriptor promises two handles, none follow.
    env.Send(process, client_thread, client, malformed);
    server->Acquire(server_thread.get());
    REQUIRE(server->ReceiveRequest(server_thread) == Kernel::ResultInvalidBufferDescriptor);
    REQUIRE(client_thread->status == Kernel::ThreadStatus::Ready);
    REQUIRE(client_thread->context.cpu_registers[0] == Kernel::ResultInvalidBufferDescriptor.raw);
    REQUIRE(server->currently_handling == nullptr);

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> valid{};
    valid[0] = IPC::MakeHeader(0x55, 1, 0);
    valid[1] = 9;
    env.Send(process, client_thread, client, valid);
    server->Acquire(server_thread.get());
    REQUIRE(server->ReceiveRequest(server_thread) == ResultSuccess);
    REQUIRE(env.ReadCommand(server_process, server_thread)[1] == 9);

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> response{};
    response[0] = IPC::MakeHeader(0x55, 1, 0);
    response[1] = ResultSuccess.raw;
    env.WriteCommand(server_process, server_thread, response);
    REQUIRE(server->ReplyRequest(server_thread) == ResultSuccess);
    REQUIRE(client_thread->status == Kernel::ThreadStatus::Ready);
    REQUIRE(env.ReadCommand(process, client_thread)[1] == ResultSuccess.raw);
}

TEST_CASE("SRV redirects one guest process through registered guest services",
          "[core][service][sm][integration]") {
    TestEnvironment env;
    auto srv = std::make_shared<Service::SM::SRV>(env.system, env.manager);
    srv->InstallAsService(env.manager);
    auto nwm_uds = std::make_shared<MockNwmUds>();
    nwm_uds->InstallAsService(env.manager);

    auto process_a = env.MakeProcess();
    auto process_b = env.MakeProcess();
    auto thread_a = env.MakeThread(process_a);
    auto thread_b = env.MakeThread(process_b);
    auto srv_session_a = env.ConnectSrv();
    auto srv_session_b = env.ConnectSrv();

    const Kernel::Luma3DS::CompatibilityContext compatibility{.enabled = true};
    const Kernel::Luma3DS::PluginAuthorization authorization{
        .plugin_loaded = true,
        .plugin_integrity_verified = true,
        .owner_process_id = process_a->process_id,
        .plugin_code_begin = 0x07000100,
        .plugin_code_end = 0x07100000,
        .capabilities =
            Kernel::Luma3DS::CapabilityBit(
                Kernel::Luma3DS::Level256Capability::PluginUdsControlIPC) |
            Kernel::Luma3DS::CapabilityBit(Kernel::Luma3DS::Level256Capability::PluginUdsDataIPC),
    };
    REQUIRE_FALSE(Kernel::Luma3DS::CanDispatchLevel256IPC(
        compatibility, authorization, process_a->process_id, 0x00100000,
        Kernel::Luma3DS::Level256Capability::PluginUdsControlIPC));
    REQUIRE(Kernel::Luma3DS::CanDispatchLevel256IPC(
        compatibility, authorization, process_a->process_id, 0x07001000,
        Kernel::Luma3DS::Level256Capability::PluginUdsControlIPC));
    const auto initialize =
        srv->ProcessPlgUdsControl(process_a->process_id, 100, 2, 0, process_a->process_id, 0);
    REQUIRE(initialize.handled);
    REQUIRE(initialize.forward_to_server);

    const auto absent = env.GetService(process_a, thread_a, srv_session_a, "nwm::UDS");
    REQUIRE(absent[1] == Service::SM::ResultServiceNotRegistered.raw);
    std::shared_ptr<Kernel::ClientPort> ordinary_uds_port;
    REQUIRE(env.manager.GetServicePort(&ordinary_uds_port, "nwm::UDS").IsSuccess());

    const auto registration =
        env.RegisterGuestService(process_b, thread_b, srv_session_b, "plg:UDS", 3);
    REQUIRE(registration[1] == ResultSuccess.raw);
    const auto plugin_server_handle = registration[3];
    const auto plugin_server_port =
        process_b->handle_table.Get<Kernel::ServerPort>(plugin_server_handle);
    REQUIRE(plugin_server_port);

    // PLGUDS_Initialize has registered process A before the service lookup; it is the only
    // process whose requested nwm::UDS name resolves to this guest LLE service.
    REQUIRE(srv->ResolveProcessService(process_a->process_id, "nwm::UDS") == "plg:UDS");

    const auto duplicate_registration =
        env.RegisterGuestService(process_b, thread_b, srv_session_b, "plg:UDS");
    REQUIRE(duplicate_registration[1] == Service::SM::ResultAlreadyRegistered.raw);

    const auto lookup_a = env.GetService(process_a, thread_a, srv_session_a, "nwm::UDS");
    const auto lookup_b = env.GetService(process_b, thread_b, srv_session_b, "nwm::UDS");
    REQUIRE(lookup_a[1] == ResultSuccess.raw);
    REQUIRE(lookup_b[1] == ResultSuccess.raw);
    auto session_a = process_a->handle_table.Get<Kernel::ClientSession>(lookup_a[3]);
    const auto session_b = process_b->handle_table.Get<Kernel::ClientSession>(lookup_b[3]);
    REQUIRE(session_a);
    REQUIRE(session_b);
    REQUIRE(session_a->parent);
    REQUIRE(session_a->parent->port);
    REQUIRE(session_a->parent->port->GetName() == "plg:UDS_Client");
    REQUIRE(session_a->GetName() == "plg:UDS_Server_Client");
    REQUIRE(Kernel::Luma3DS::ClassifyLevel256IpcPortName(session_a->parent->port->GetName()) ==
            Kernel::Luma3DS::Level256IpcService::PluginUds);
    REQUIRE(Kernel::Luma3DS::CanDispatchLevel256RoutedIPC(
        compatibility, authorization, process_a->process_id,
        srv->IsLevel256Connected(process_a->process_id),
        Kernel::Luma3DS::Level256Capability::PluginUdsDataIPC));
    REQUIRE_FALSE(Kernel::Luma3DS::CanDispatchLevel256RoutedIPC(
        compatibility, authorization, process_b->process_id,
        srv->IsLevel256Connected(process_a->process_id),
        Kernel::Luma3DS::Level256Capability::PluginUdsDataIPC));
    const auto translated_scan = Service::SM::TranslateLevel256UdsCommand(15, 16, 4);
    REQUIRE((translated_scan == Service::SM::UdsCommandShape{15, 20, 0}));

    // The registered guest service has no HLE handler. Connect the real server endpoint and
    // exercise the same request/reply buffer translator used by SVC::ReplyAndReceive.
    std::shared_ptr<Kernel::ServerSession> server_session;
    REQUIRE(plugin_server_port->Accept(&server_session) == ResultSuccess);
    REQUIRE(server_session);

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> request{};
    request[0] = IPC::MakeHeader(0x55, 1, 0);
    request[1] = 7;
    env.WriteCommand(process_a, thread_a, request);
    thread_a->status = Kernel::ThreadStatus::Running;
    REQUIRE(session_a->SendSyncRequest(thread_a) == ResultSuccess);
    REQUIRE(thread_a->status == Kernel::ThreadStatus::WaitIPC);
    REQUIRE_FALSE(server_session->ShouldWait(thread_b.get()));

    server_session->Acquire(thread_b.get());
    REQUIRE(server_session->currently_handling == thread_a);
    REQUIRE(server_session->ReceiveRequest(thread_b) == ResultSuccess);
    const auto received_request = env.ReadCommand(process_b, thread_b);
    REQUIRE(received_request[0] == IPC::MakeHeader(0x55, 1, 0));
    REQUIRE(received_request[1] == 7);

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> response{};
    response[0] = IPC::MakeHeader(0x55, 2, 0);
    response[1] = ResultSuccess.raw;
    response[2] = received_request[1] + 100;
    env.WriteCommand(process_b, thread_b, response);
    REQUIRE(server_session->ReplyRequest(thread_b) == ResultSuccess);
    const auto returned_response = env.ReadCommand(process_a, thread_a);
    REQUIRE(returned_response[0] == IPC::MakeHeader(0x55, 2, 0));
    REQUIRE(returned_response[1] == ResultSuccess.raw);
    REQUIRE(returned_response[2] == 107);

    // Exercise the real guest LLE session queue and ReceiveRequest/ReplyRequest translators
    // with the command 15 header shape expected after the SendSyncRequest adapter. This test
    // validates the session leg; the SVC's actual header rewrite is covered by runtime tracing.
    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> scan_request{};
    scan_request[0] = IPC::MakeHeader(15, 20, 0);
    scan_request[1] = 0x12345678;
    env.WriteCommand(process_a, thread_a, scan_request);
    thread_a->status = Kernel::ThreadStatus::Running;
    REQUIRE(session_a->SendSyncRequest(thread_a) == ResultSuccess);
    REQUIRE(thread_a->status == Kernel::ThreadStatus::WaitIPC);
    server_session->Acquire(thread_b.get());
    REQUIRE(server_session->ReceiveRequest(thread_b) == ResultSuccess);
    const auto received_scan = env.ReadCommand(process_b, thread_b);
    REQUIRE(received_scan[0] == IPC::MakeHeader(15, 20, 0));
    REQUIRE(received_scan[1] == 0x12345678);

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> scan_response{};
    scan_response[0] = IPC::MakeHeader(15, 2, 0);
    scan_response[1] = ResultSuccess.raw;
    scan_response[2] = 3; // Sentinel payload word; this test does not define the UDS ABI.
    env.WriteCommand(process_b, thread_b, scan_response);
    REQUIRE(server_session->ReplyRequest(thread_b) == ResultSuccess);
    const auto returned_scan = env.ReadCommand(process_a, thread_a);
    REQUIRE(returned_scan[0] == IPC::MakeHeader(15, 2, 0));
    REQUIRE(returned_scan[1] == ResultSuccess.raw);
    REQUIRE(returned_scan[2] == 3);

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> ordinary_request{};
    ordinary_request[0] = IPC::MakeHeader(0x55, 1, 0);
    ordinary_request[1] = 7;
    env.Send(process_b, thread_b, session_b, ordinary_request);
    REQUIRE(env.ReadCommand(process_b, thread_b)[2] == 8);

    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> unregister{};
    unregister[0] = IPC::MakeHeader(4, 3, 0);
    std::memcpy(&unregister[1], "plg:UDS", 7);
    unregister[3] = 7;
    env.Send(process_a, thread_a, srv_session_a, unregister);
    REQUIRE(env.ReadCommand(process_a, thread_a)[1] == Service::SM::ResultAccessDenied.raw);
    std::shared_ptr<Kernel::ClientPort> registered_port;
    REQUIRE(env.manager.GetServicePort(&registered_port, "plg:UDS").IsSuccess());
    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> unregister_b{};
    unregister_b[0] = IPC::MakeHeader(4, 3, 0);
    std::memcpy(&unregister_b[1], "plg:UDS", 7);
    unregister_b[3] = 7;
    env.Send(process_b, thread_b, srv_session_b, unregister_b);
    REQUIRE(env.ReadCommand(process_b, thread_b)[1] == ResultSuccess.raw);
    std::shared_ptr<Kernel::ClientPort> removed_port;
    REQUIRE(env.manager.GetServicePort(&removed_port, "plg:UDS") ==
            Service::SM::ResultServiceNotRegistered);
    REQUIRE(srv->ResolveProcessService(process_a->process_id, "nwm::UDS") == "plg:UDS");

    request[1] = 9;
    env.WriteCommand(process_a, thread_a, request);
    thread_a->status = Kernel::ThreadStatus::Running;
    REQUIRE(session_a->SendSyncRequest(thread_a) == ResultSuccess);
    server_session->Acquire(thread_b.get());
    REQUIRE(server_session->ReceiveRequest(thread_b) == ResultSuccess);
    REQUIRE(env.ReadCommand(process_b, thread_b)[1] == 9);
    response[2] = 109;
    env.WriteCommand(process_b, thread_b, response);
    REQUIRE(server_session->ReplyRequest(thread_b) == ResultSuccess);
    REQUIRE(env.ReadCommand(process_a, thread_a)[2] == 109);

    const auto reregistration =
        env.RegisterGuestService(process_b, thread_b, srv_session_b, "plg:UDS");
    REQUIRE(reregistration[1] == ResultSuccess.raw);
    const auto replacement_server_port =
        process_b->handle_table.Get<Kernel::ServerPort>(reregistration[3]);
    REQUIRE(replacement_server_port);
    REQUIRE(replacement_server_port != plugin_server_port);
    const auto relayed_lookup = env.GetService(process_a, thread_a, srv_session_a, "nwm::UDS");
    REQUIRE(relayed_lookup[1] == ResultSuccess.raw);

    srv->OnProcessExit(process_a->process_id);
    REQUIRE_FALSE(srv->ResolveProcessService(process_a->process_id, "nwm::UDS"));

    REQUIRE(process_a->handle_table.Close(lookup_a[3]) == ResultSuccess);
    session_a.reset();
    REQUIRE(server_session->parent->client == nullptr);
    REQUIRE(server_session->currently_handling == nullptr);

    env.manager.UnregisterServicesForProcess(process_b->process_id);
    std::shared_ptr<Kernel::ClientPort> exited_process_port;
    REQUIRE(env.manager.GetServicePort(&exited_process_port, "plg:UDS") ==
            Service::SM::ResultServiceNotRegistered);
}

TEST_CASE("ServiceManager resolves SRV from its production named port",
          "[core][service][sm][integration][level256]") {
    TestEnvironment env;
    auto srv = std::make_shared<Service::SM::SRV>(env.system, env.manager);
    srv->InstallAsNamedPort(env.kernel);

    // Production installs srv: as a kernel NamedPort, not in registered_services.
    REQUIRE_FALSE(env.manager.GetService<Service::SM::SRV>("srv:"));
    REQUIRE(env.manager.GetSRV() == srv);

    auto process = env.MakeProcess();
    const auto initialized =
        srv->ProcessPlgUdsControl(process->process_id, 100, 2, 0, process->process_id, 0);
    REQUIRE(initialized.handled);
    REQUIRE(initialized.forward_to_server);
    REQUIRE(srv->ResolveProcessService(process->process_id, "nwm::UDS") == "plg:UDS");
    REQUIRE_FALSE(srv->ResolveProcessService(process->process_id + 1, "nwm::UDS"));

    std::shared_ptr<Kernel::ServerPort> uds_server_port;
    REQUIRE(env.manager.RegisterService(&uds_server_port, "plg:UDS", 4) == ResultSuccess);
    std::shared_ptr<Kernel::ClientPort> resolved_port;
    REQUIRE(env.manager.GetServicePort(
                &resolved_port, *srv->ResolveProcessService(process->process_id, "nwm::UDS")) ==
            ResultSuccess);
    REQUIRE(resolved_port->GetName() == "plg:UDS_Client");

    // Reacquire from the current kernel NamedPort table so restored handler objects are used.
    auto restored_srv = std::make_shared<Service::SM::SRV>(env.system, env.manager);
    auto [restored_server_port, restored_client_port] = env.kernel.CreatePortPair(64, "srv:");
    restored_server_port->SetHleHandler(restored_srv);
    env.kernel.named_ports.insert_or_assign("srv:", restored_client_port);
    REQUIRE(env.manager.GetSRV() == restored_srv);

    auto [wrong_server_port, wrong_client_port] = env.kernel.CreatePortPair(4, "srv:");
    wrong_server_port->SetHleHandler(std::make_shared<MockNwmUds>());
    env.kernel.named_ports.insert_or_assign("srv:", wrong_client_port);
    REQUIRE_FALSE(env.manager.GetSRV());
}

TEST_CASE("SRV wakes every process-scoped delayed lookup when guest service registers",
          "[core][service][sm][integration]") {
    TestEnvironment env;
    auto srv = std::make_shared<Service::SM::SRV>(env.system, env.manager);
    srv->InstallAsService(env.manager);

    auto process_a = env.MakeProcess();
    auto process_b = env.MakeProcess();
    auto thread_a1 = env.MakeThread(process_a);
    auto thread_a2 = env.MakeThread(process_a);
    auto thread_b = env.MakeThread(process_b);
    auto thread_b_register = env.MakeThread(process_b);
    const auto srv_a1 = env.ConnectSrv();
    const auto srv_a2 = env.ConnectSrv();
    const auto srv_b = env.ConnectSrv();
    srv->SetLevel256Redirect(*process_a, true, false);

    // A waits for the redirected service name. B waits for the ordinary service name. Register
    // only the redirected target and make sure the callback resolves the PID-scoped target again.
    env.GetService(process_a, thread_a1, srv_a1, "nwm::UDS", 0);
    env.GetService(process_a, thread_a2, srv_a2, "nwm::UDS", 0);
    env.GetService(process_b, thread_b, srv_b, "nwm::UDS", 0);
    REQUIRE(thread_a1->status == Kernel::ThreadStatus::WaitHleEvent);
    REQUIRE(thread_a2->status == Kernel::ThreadStatus::WaitHleEvent);
    REQUIRE(thread_b->status == Kernel::ThreadStatus::WaitHleEvent);

    const auto registration =
        env.RegisterGuestService(process_b, thread_b_register, srv_b, "plg:UDS");
    REQUIRE(registration[1] == ResultSuccess.raw);
    REQUIRE(thread_a1->status == Kernel::ThreadStatus::Ready);
    REQUIRE(thread_a2->status == Kernel::ThreadStatus::Ready);
    REQUIRE(thread_b->status == Kernel::ThreadStatus::WaitHleEvent);

    const auto a1_response = env.ReadCommand(process_a, thread_a1);
    const auto a2_response = env.ReadCommand(process_a, thread_a2);
    REQUIRE(a1_response[1] == ResultSuccess.raw);
    REQUIRE(a2_response[1] == ResultSuccess.raw);
    REQUIRE(process_a->handle_table.Get<Kernel::ClientSession>(a1_response[3]));
    REQUIRE(process_a->handle_table.Get<Kernel::ClientSession>(a2_response[3]));
}
