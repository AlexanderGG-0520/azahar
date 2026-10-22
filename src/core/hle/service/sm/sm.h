// Copyright 2017-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/unordered_map.hpp>
#include "core/hle/kernel/client_port.h"
#include "core/hle/kernel/object.h"
#include "core/hle/kernel/server_port.h"
#include "core/hle/result.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class ClientSession;
class KernelSystem;
class SessionRequestHandler;
} // namespace Kernel

namespace Service::SM {

class SRV;

constexpr Result ResultServiceNotRegistered(1, ErrorModule::SRV, ErrorSummary::WouldBlock,
                                            ErrorLevel::Temporary); // 0xD0406401
constexpr Result ResultMaxConnectionsReached(2, ErrorModule::SRV, ErrorSummary::WouldBlock,
                                             ErrorLevel::Temporary); // 0xD0406402
constexpr Result ResultInvalidNameSize(5, ErrorModule::SRV, ErrorSummary::WrongArgument,
                                       ErrorLevel::Permanent); // 0xD9006405
constexpr Result ResultAccessDenied(6, ErrorModule::SRV, ErrorSummary::InvalidArgument,
                                    ErrorLevel::Permanent); // 0xD8E06406
constexpr Result ResultNameContainsNul(7, ErrorModule::SRV, ErrorSummary::WrongArgument,
                                       ErrorLevel::Permanent); // 0xD9006407
constexpr Result ResultAlreadyRegistered(ErrorDescription::AlreadyExists, ErrorModule::OS,
                                         ErrorSummary::WrongArgument,
                                         ErrorLevel::Permanent); // 0xD9001BFC

class ServiceManager {
public:
    static void InstallInterfaces(Core::System& system);

    explicit ServiceManager(Core::System& system);
    ServiceManager(Core::System& system, Kernel::KernelSystem& kernel);

    Result RegisterService(std::shared_ptr<Kernel::ServerPort>* out_server_port, std::string name,
                           u32 max_sessions, u32 owner_process_id = 0);
    Result UnregisterService(const std::string& name, u32 owner_process_id);
    void UnregisterServicesForProcess(u32 owner_process_id);
    Result GetServicePort(std::shared_ptr<Kernel::ClientPort>* out_client_port,
                          const std::string& name);
    Result ConnectToService(std::shared_ptr<Kernel::ClientSession>* out_client_session,
                            const std::string& name);
    // For IPC Recorder
    std::string GetServiceNameByPortId(u32 port) const;

    template <typename T>
    std::shared_ptr<T> GetService(const std::string& service_name) const {
        static_assert(std::is_base_of_v<Kernel::SessionRequestHandler, T>,
                      "Not a base of ServiceFrameworkBase");
        auto service = registered_services.find(service_name);
        if (service == registered_services.end()) {
            LOG_DEBUG(Service, "Can't find service: {}", service_name);
            return nullptr;
        }
        auto port = service->second->GetServerPort();
        if (port == nullptr) {
            return nullptr;
        }
        return std::static_pointer_cast<T>(port->hle_handler);
    }

private:
    Core::System& system;
    Kernel::KernelSystem& kernel;
    std::weak_ptr<SRV> srv_interface;

    /// Map of registered services, retrieved using GetServicePort or ConnectToService.
    std::unordered_map<std::string, std::shared_ptr<Kernel::ClientPort>> registered_services;
    /// 0 denotes a service registered by the emulator, otherwise its guest owner PID.
    std::unordered_map<std::string, u32> service_owners;

    // For IPC Recorder
    /// client port Object id -> service name
    std::unordered_map<u32, std::string> registered_services_inverse;

    template <class Archive>
    void save(Archive& ar, const unsigned int) const {
        DEBUG_SERIALIZATION_POINT;
        ar << registered_services;
        ar << service_owners;
    }

    template <class Archive>
    void load(Archive& ar, const unsigned int file_version) {
        DEBUG_SERIALIZATION_POINT;
        ar >> registered_services;
        if (file_version > 0) {
            ar >> service_owners;
        } else {
            service_owners.clear();
            for (const auto& service : registered_services) {
                service_owners.emplace(service.first, 0);
            }
        }
        registered_services_inverse.clear();
        for (const auto& pair : registered_services) {
            registered_services_inverse.emplace(pair.second->GetObjectId(), pair.first);
        }
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()

    friend class boost::serialization::access;
};

} // namespace Service::SM

BOOST_CLASS_VERSION(Service::SM::ServiceManager, 1)
