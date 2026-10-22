// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <boost/serialization/export.hpp>
#include "core/hle/service/sm/process_service_redirect.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class Event;
class HLERequestContext;
class Process;
class Semaphore;
} // namespace Kernel

namespace Service::SM {

/// Interface to "srv:" service
class SRV final : public ServiceFramework<SRV> {
public:
    explicit SRV(Core::System& system);
    SRV(Core::System& system, ServiceManager& service_manager);
    ~SRV();

    void SetLevel256Redirect(Kernel::Process& process, bool enabled, bool dlp_enabled);
    void OnProcessExit(u32 process_id);
    std::optional<std::string> ResolveProcessService(u32 process_id,
                                                     const std::string& service_name) const;
    PlgUdsControlResult ProcessPlgUdsControl(u32 process_id, u16 command_id,
                                             u8 normal_params, u8 translate_params, u32 arg1,
                                             u32 arg2);
    bool IsLevel256Connected(u32 process_id) const;
    bool IsLevel256Paused(u32 process_id) const;
    bool Level256SignalOnAttach(u32 process_id) const;
    bool Level256SignalOnDetach(u32 process_id) const;
    void SetLevel256SignalOnDetach(u32 process_id, bool enabled);

    class ThreadCallback;

private:
    struct DelayedServiceHandle {
        u32 process_id{};
        std::shared_ptr<Kernel::Event> event;

        template <class Archive>
        void serialize(Archive& ar, const unsigned int) {
            ar & process_id;
            ar & event;
        }
        friend class boost::serialization::access;
    };

    void RegisterClient(Kernel::HLERequestContext& ctx);
    void EnableNotification(Kernel::HLERequestContext& ctx);
    void GetServiceHandle(Kernel::HLERequestContext& ctx);
    void UnregisterService(Kernel::HLERequestContext& ctx);
    void Subscribe(Kernel::HLERequestContext& ctx);
    void Unsubscribe(Kernel::HLERequestContext& ctx);
    void PublishToSubscriber(Kernel::HLERequestContext& ctx);
    void RegisterService(Kernel::HLERequestContext& ctx);

    Core::System& system;
    ServiceManager& service_manager;
    ProcessServiceRedirect process_service_redirect;
    std::shared_ptr<Kernel::Semaphore> notification_semaphore;
    std::unordered_map<std::string, std::vector<DelayedServiceHandle>>
        get_service_handle_delayed_map;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

} // namespace Service::SM

SERVICE_CONSTRUCT(Service::SM::SRV)
BOOST_CLASS_EXPORT_KEY(Service::SM::SRV)
BOOST_CLASS_EXPORT_KEY(Service::SM::SRV::ThreadCallback)
