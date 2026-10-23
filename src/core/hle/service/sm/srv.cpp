// Copyright 2016-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <tuple>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include "common/archives.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/hle_ipc.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/kernel/semaphore.h"
#include "core/hle/kernel/server_port.h"
#include "core/hle/kernel/server_session.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/service/plgldr/plgldr.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/sm/srv.h"

SERVICE_CONSTRUCT_IMPL(Service::SM::SRV)
SERIALIZE_EXPORT_IMPL(Service::SM::SRV)

namespace Service::SM {

template <class Archive>
void SRV::serialize(Archive& ar, const unsigned int) {
    DEBUG_SERIALIZATION_POINT;
    ar& boost::serialization::base_object<Kernel::SessionRequestHandler>(*this);
    ar & notification_semaphore;
    ar & get_service_handle_delayed_map;
    ar & process_service_redirect;
}

constexpr int MAX_PENDING_NOTIFICATIONS = 16;

static bool IsLevel256ServiceName(const std::string& name) {
    return name == "plg:UDS" || name == "plg:DLP" || name == "plg:FKCL" || name == "nwm::UDS" ||
           name == "dlp:SRVR" || name == "dlp:CLNT" || name == "dlp:FKCL";
}

/**
 * SRV::RegisterClient service function
 *  Inputs:
 *      0: 0x00010002
 *      1: ProcessId Header (must be 0x20)
 *  Outputs:
 *      0: 0x00010040
 *      1: Result
 */
void SRV::RegisterClient(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    const auto pid_descriptor = rp.Pop<u32>();
    if (pid_descriptor != IPC::CallingPidDesc()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(IPC::ResultInvalidBufferDescriptor);
        return;
    }
    const auto caller_pid = rp.Pop<u32>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
    LOG_WARNING(Service_SRV, "(STUBBED) called. Caller PID={}", caller_pid);
}

/**
 * SRV::EnableNotification service function
 *  Inputs:
 *      0: 0x00020000
 *  Outputs:
 *      0: 0x00020042
 *      1: Result
 *      2: Translation descriptor: 0x20
 *      3: Handle to semaphore signaled on process notification
 */
void SRV::EnableNotification(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    notification_semaphore =
        system.Kernel().CreateSemaphore(0, MAX_PENDING_NOTIFICATIONS, "SRV:Notification").Unwrap();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(notification_semaphore);
    LOG_WARNING(Service_SRV, "(STUBBED) called");
}

class SRV::ThreadCallback : public Kernel::HLERequestContext::WakeupCallback {

public:
    explicit ThreadCallback(Core::System& system_, std::string name_)
        : system(system_), service_manager(system_.ServiceManager()), name(name_) {}
    ThreadCallback(Core::System& system_, ServiceManager& service_manager_, std::string name_)
        : system(system_), service_manager(service_manager_), name(name_) {}

    void WakeUp(std::shared_ptr<Kernel::Thread> thread, Kernel::HLERequestContext& ctx,
                Kernel::ThreadWakeupReason reason) {
        LOG_ERROR(Service_SRV, "called service={} wakeup", name);
        const auto process = thread ? thread->owner_process.lock() : nullptr;
        const auto srv = service_manager.GetSRV();
        const auto redirected =
            process && srv ? srv->ResolveProcessService(process->process_id, name) : std::nullopt;
        const auto& resolved_name = redirected ? *redirected : name;
        std::shared_ptr<Kernel::ClientPort> client_port;
        const auto port_result =
            service_manager.GetServicePort(std::addressof(client_port), resolved_name);
        if (port_result.IsError()) {
            IPC::RequestBuilder rb(ctx, 0x5, 1, 0);
            rb.Push(port_result);
            return;
        }

        std::shared_ptr<Kernel::ClientSession> session;
        auto result = client_port->Connect(std::addressof(session));
        if (result.IsSuccess()) {
            LOG_DEBUG(Service_SRV, "called service={} -> session={}", name, session->GetObjectId());
            IPC::RequestBuilder rb(ctx, 0x5, 1, 2);
            rb.Push(result);
            rb.PushMoveObjects(std::move(session));
        } else if (result == Kernel::ResultMaxConnectionsReached) {
            LOG_ERROR(Service_SRV, "called service={} -> ResultMaxConnectionsReached", name);
            UNREACHABLE();
        } else {
            LOG_ERROR(Service_SRV, "called service={} -> error 0x{:08X}", name, result.raw);
            IPC::RequestBuilder rb(ctx, 0x5, 1, 0);
            rb.Push(result);
        }
    }

private:
    Core::System& system;
    ServiceManager& service_manager;
    std::string name;

    ThreadCallback()
        : system(Core::Global<Core::System>()), service_manager(system.ServiceManager()) {}

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar& boost::serialization::base_object<Kernel::HLERequestContext::WakeupCallback>(*this);
        ar & name;
    }
    friend class boost::serialization::access;
};

/**
 * SRV::GetServiceHandle service function
 *  Inputs:
 *      0: 0x00050100
 *      1-2: 8-byte UTF-8 service name
 *      3: Name length
 *      4: Flags (bit0: if not set, return port-handle if session-handle unavailable)
 *  Outputs:
 *      1: Result
 *      3: Service handle
 */
void SRV::GetServiceHandle(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto name_buf = rp.PopRaw<std::array<char, 8>>();
    std::size_t name_len = rp.Pop<u32>();
    u32 flags = rp.Pop<u32>();

    bool wait_until_available = (flags & 1) == 0;

    if (name_len > Service::kMaxPortSize) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultInvalidNameSize);
        LOG_ERROR(Service_SRV, "called name_len=0x{:X} -> ResultInvalidNameSize", name_len);
        return;
    }
    std::string name(name_buf.data(), name_len);

    // TODO(yuriks): Permission checks go here

    const auto process = ctx.ClientThread() ? ctx.ClientThread()->owner_process.lock() : nullptr;
    const auto redirect = process ? ResolveProcessService(process->process_id, name) : std::nullopt;
    const auto& resolved_name = redirect ? *redirect : name;
    const auto plugin_loader = PLGLDR::GetService(system);
    const bool trace_level256_uds = name == "nwm::UDS" && process && plugin_loader &&
                                    plugin_loader->ShouldTraceLevel256NetworkIPC(*process);
    const auto redirect_state =
        process ? GetLevel256RedirectState(process->process_id) : std::nullopt;
    const auto trace_stage = [&](const char* stage, u32 result, u32 port_id,
                                 const std::string& port_name, u32 client_session_id,
                                 u32 server_session_id) {
        if (!trace_level256_uds || !plugin_loader->ConsumeLevel256NetworkIPCTraceSlot()) {
            return;
        }
        LOG_INFO(Service_SRV,
                 "Level256 E2E service stage={} pid={} thread={} requested={} resolved={} "
                 "redirected={} redirect_present={} log_mode={} paused={} dlp_enabled={} "
                 "port={} port_id={} client_session={} server_session={} result=0x{:08X}",
                 stage, process->process_id, ctx.ClientThread()->GetThreadId(), name, resolved_name,
                 redirect.has_value(), redirect_state.has_value(),
                 redirect_state ? redirect_state->is_log_mode : false,
                 redirect_state ? redirect_state->is_paused : false,
                 redirect_state ? redirect_state->dlp_enabled : false, port_name, port_id,
                 client_session_id, server_session_id, result);
    };
    trace_stage("resolve", ResultSuccess.raw, 0, {}, 0, 0);

    auto get_handle = std::make_shared<ThreadCallback>(system, service_manager, resolved_name);

    std::shared_ptr<Kernel::ClientPort> client_port;
    auto result = service_manager.GetServicePort(std::addressof(client_port), resolved_name);
    trace_stage("port_lookup", result.raw, client_port ? client_port->GetObjectId() : 0,
                client_port ? client_port->GetName() : std::string{}, 0, 0);
    if (result.IsError()) {
        if (wait_until_available && result == ResultServiceNotRegistered) {
            LOG_INFO(Service_SRV, "called service={} delayed", resolved_name);
            std::shared_ptr<Kernel::Event> get_service_handle_event =
                ctx.SleepClientThread("GetServiceHandle", std::chrono::nanoseconds(-1), get_handle);
            get_service_handle_delayed_map[resolved_name].push_back(DelayedServiceHandle{
                process ? process->process_id : 0, std::move(get_service_handle_event)});
            return;
        } else {
            IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
            rb.Push(result);
            LOG_ERROR(Service_SRV, "called service={} -> error 0x{:08X}", resolved_name,
                      result.raw);
            return;
        }
    }

    std::shared_ptr<Kernel::ClientSession> session;
    result = client_port->Connect(std::addressof(session));
    trace_stage("connect", result.raw, client_port->GetObjectId(), client_port->GetName(),
                session ? session->GetObjectId() : 0,
                session && session->parent && session->parent->server
                    ? session->parent->server->GetObjectId()
                    : 0);
    if (result.IsSuccess()) {
        LOG_DEBUG(Service_SRV, "called service={} -> session={}", resolved_name,
                  session->GetObjectId());
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(result);
        rb.PushMoveObjects(std::move(session));
    } else if (result == Kernel::ResultMaxConnectionsReached && wait_until_available) {
        LOG_WARNING(Service_SRV, "called service={} -> ResultMaxConnectionsReached", resolved_name);
        // TODO(Subv): Put the caller guest thread to sleep until this port becomes available again.
        UNIMPLEMENTED_MSG("Unimplemented wait until port {} is available.", name);
    } else {
        LOG_ERROR(Service_SRV, "called service={} -> error 0x{:08X}", resolved_name, result.raw);
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(result);
    }
}

void SRV::UnregisterService(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto name_buf = rp.PopRaw<std::array<char, 8>>();
    const std::size_t name_len = rp.Pop<u32>();
    if (name_len == 0 || name_len > name_buf.size()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultInvalidNameSize);
        return;
    }

    const auto process = ctx.ClientThread() ? ctx.ClientThread()->owner_process.lock() : nullptr;
    if (!process) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultAccessDenied);
        return;
    }

    const std::string name(name_buf.data(), name_len);
    const auto result = service_manager.UnregisterService(name, process->process_id);
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(result);
}

/**
 * SRV::Subscribe service function
 *  Inputs:
 *      0: 0x00090040
 *      1: Notification ID
 *  Outputs:
 *      0: 0x00090040
 *      1: Result
 */
void SRV::Subscribe(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u32 notification_id = rp.Pop<u32>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
    LOG_WARNING(Service_SRV, "(STUBBED) called, notification_id=0x{:X}", notification_id);
}

/**
 * SRV::Unsubscribe service function
 *  Inputs:
 *      0: 0x000A0040
 *      1: Notification ID
 *  Outputs:
 *      0: 0x000A0040
 *      1: Result
 */
void SRV::Unsubscribe(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u32 notification_id = rp.Pop<u32>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
    LOG_WARNING(Service_SRV, "(STUBBED) called, notification_id=0x{:X}", notification_id);
}

/**
 * SRV::PublishToSubscriber service function
 *  Inputs:
 *      0: 0x000C0080
 *      1: Notification ID
 *      2: Flags (bit0: only fire if not fired, bit1: report errors)
 *  Outputs:
 *      0: 0x000C0040
 *      1: Result
 */
void SRV::PublishToSubscriber(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u32 notification_id = rp.Pop<u32>();
    u8 flags = rp.Pop<u8>();

    // Handle notification 0x203 in HLE, as this one may be used by homebrew to power off the
    // console. Normally, this is handled by NS. If notification handling is properly implemented,
    // this piece of code should be removed, and handled by subscribing from NS instead.
    if (notification_id == 0x203) {
        system.RequestShutdown();
    } else {
        LOG_WARNING(Service_SRV, "(STUBBED) called, notification_id=0x{:X}, flags={}",
                    notification_id, flags);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void SRV::RegisterService(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    auto name_buf = rp.PopRaw<std::array<char, 8>>();
    std::size_t name_len = rp.Pop<u32>();
    u32 max_sessions = rp.Pop<u32>();

    if (name_len == 0 || name_len > name_buf.size()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultInvalidNameSize);
        return;
    }
    const auto process = ctx.ClientThread() ? ctx.ClientThread()->owner_process.lock() : nullptr;
    if (!process) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultAccessDenied);
        return;
    }
    std::string name(name_buf.data(), name_len);

    const auto caller_thread = ctx.ClientThread();
    bool trace_plugin_service = false;
    u32 plugin_owner_pid = UINT32_MAX;
    if (IsLevel256ServiceName(name)) {
        const auto plugin_loader = Service::PLGLDR::GetService(system);
        if (plugin_loader) {
            auto& plugin = plugin_loader->GetPluginLoaderContext();
            // Observe relevant service registration while a plugin is active, regardless of
            // which guest process owns the server. Level256 may publish a service from a
            // process other than the one containing the 3GX plugin.
            if (plugin.plugin_loaded && plugin.plugin_service_trace_count < 16) {
                ++plugin.plugin_service_trace_count;
                trace_plugin_service = true;
                plugin_owner_pid = plugin.plugin_process_id;
            }
        }
    }
    if (trace_plugin_service) {
        LOG_INFO(Service_SRV,
                 "3GX-related SRV RegisterService request caller_pid={} thread={} "
                 "plugin_owner_pid={} name={} max_sessions={}",
                 process->process_id, caller_thread ? caller_thread->GetThreadId() : 0,
                 plugin_owner_pid, name, max_sessions);
    }

    std::shared_ptr<Kernel::ServerPort> port;
    auto result = service_manager.RegisterService(std::addressof(port), name, max_sessions,
                                                  process->process_id);

    if (result.IsError()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(result);
        LOG_ERROR(Service_SRV, "called service={} -> error 0x{:08X}", name, result.raw);
        if (trace_plugin_service) {
            LOG_INFO(Service_SRV,
                     "3GX-related SRV RegisterService result caller_pid={} thread={} "
                     "plugin_owner_pid={} name={} result=0x{:08X}",
                     process->process_id, caller_thread ? caller_thread->GetThreadId() : 0,
                     plugin_owner_pid, name, result.raw);
        }
        return;
    }

    auto it = get_service_handle_delayed_map.find(name);
    if (it != get_service_handle_delayed_map.end()) {
        for (const auto& waiting : it->second) {
            waiting.event->Signal();
        }
        get_service_handle_delayed_map.erase(it);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushMoveObjects(std::move(port));
    if (trace_plugin_service) {
        LOG_INFO(Service_SRV,
                 "3GX-related SRV RegisterService result caller_pid={} thread={} "
                 "plugin_owner_pid={} name={} result=0x{:08X}",
                 process->process_id, caller_thread ? caller_thread->GetThreadId() : 0,
                 plugin_owner_pid, name, ResultSuccess.raw);
    }
}

SRV::SRV(Core::System& system) : SRV(system, system.ServiceManager()) {}

SRV::SRV(Core::System& system, ServiceManager& service_manager)
    : ServiceFramework("srv:", 64), system(system), service_manager(service_manager) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, &SRV::RegisterClient, "RegisterClient"},
        {0x0002, &SRV::EnableNotification, "EnableNotification"},
        {0x0003, &SRV::RegisterService, "RegisterService"},
        {0x0004, &SRV::UnregisterService, "UnregisterService"},
        {0x0005, &SRV::GetServiceHandle, "GetServiceHandle"},
        {0x0006, nullptr, "RegisterPort"},
        {0x0007, nullptr, "UnregisterPort"},
        {0x0008, nullptr, "GetPort"},
        {0x0009, &SRV::Subscribe, "Subscribe"},
        {0x000A, &SRV::Unsubscribe, "Unsubscribe"},
        {0x000B, nullptr, "ReceiveNotification"},
        {0x000C, &SRV::PublishToSubscriber, "PublishToSubscriber"},
        {0x000D, nullptr, "PublishAndGetSubscriber"},
        {0x000E, nullptr, "IsServiceRegistered"},
        // clang-format on
    };
    RegisterHandlers(functions);
}

SRV::~SRV() = default;

void SRV::SetLevel256Redirect(Kernel::Process& process, bool enabled, bool dlp_enabled) {
    if (enabled && process.status != Kernel::ProcessStatus::Running) {
        return;
    }
    process_service_redirect.SetLevel256(process.process_id, enabled, dlp_enabled);
}

void SRV::OnProcessExit(u32 process_id) {
    process_service_redirect.OnProcessExit(process_id);
    for (auto waiting = get_service_handle_delayed_map.begin();
         waiting != get_service_handle_delayed_map.end();) {
        std::erase_if(waiting->second, [process_id](const DelayedServiceHandle& request) {
            return request.process_id == process_id;
        });
        if (waiting->second.empty()) {
            waiting = get_service_handle_delayed_map.erase(waiting);
        } else {
            ++waiting;
        }
    }
}

std::optional<std::string> SRV::ResolveProcessService(u32 process_id,
                                                      const std::string& service_name) const {
    return process_service_redirect.Resolve(process_id, service_name);
}

PlgUdsControlResult SRV::ProcessPlgUdsControl(u32 process_id, u16 command_id, u8 normal_params,
                                              u8 translate_params, u32 arg1, u32 arg2) {
    return process_service_redirect.ProcessPlgUdsControl(process_id, command_id, normal_params,
                                                         translate_params, arg1, arg2);
}

bool SRV::IsLevel256Connected(u32 process_id) const {
    return process_service_redirect.IsConnectedProcess(process_id);
}

bool SRV::IsLevel256Paused(u32 process_id) const {
    return process_service_redirect.IsPaused(process_id);
}

std::optional<ProcessServiceRedirect::State> SRV::GetLevel256RedirectState(u32 process_id) const {
    return process_service_redirect.GetState(process_id);
}

bool SRV::Level256SignalOnAttach(u32 process_id) const {
    return process_service_redirect.SignalOnAttach(process_id);
}

bool SRV::Level256SignalOnDetach(u32 process_id) const {
    return process_service_redirect.SignalOnDetach(process_id);
}

void SRV::SetLevel256SignalOnDetach(u32 process_id, bool enabled) {
    process_service_redirect.SetSignalOnDetach(process_id, enabled);
}

} // namespace Service::SM

SERIALIZE_EXPORT_IMPL(Service::SM::SRV::ThreadCallback)
