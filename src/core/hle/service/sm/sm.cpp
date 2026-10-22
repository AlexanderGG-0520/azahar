// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <tuple>
#include "common/assert.h"
#include "core/core.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/result.h"
#include "core/hle/service/sm/sm.h"
#include "core/hle/service/sm/srv.h"

namespace Service::SM {

static Result ValidateServiceName(const std::string& name) {
    R_UNLESS(name.size() > 0 && name.size() <= 8, ResultInvalidNameSize);
    R_UNLESS(name.find('\0') == std::string::npos, ResultNameContainsNul);
    return ResultSuccess;
}

ServiceManager::ServiceManager(Core::System& system) : system(system), kernel(system.Kernel()) {}

ServiceManager::ServiceManager(Core::System& system, Kernel::KernelSystem& kernel)
    : system(system), kernel(kernel) {}

void ServiceManager::InstallInterfaces(Core::System& system) {
    ASSERT(system.ServiceManager().srv_interface.expired());

    auto srv = std::make_shared<SRV>(system);
    srv->InstallAsNamedPort(system.Kernel());
    system.ServiceManager().srv_interface = srv;
}

Result ServiceManager::RegisterService(std::shared_ptr<Kernel::ServerPort>* out_server_port,
                                       std::string name, u32 max_sessions,
                                       u32 owner_process_id) {
    R_TRY(ValidateServiceName(name));
    R_UNLESS(registered_services.find(name) == registered_services.end(), ResultAlreadyRegistered);

    const auto [server_port, client_port] = kernel.CreatePortPair(max_sessions, name);
    registered_services_inverse.emplace(client_port->GetObjectId(), name);
    service_owners.emplace(name, owner_process_id);
    registered_services.emplace(std::move(name), std::move(client_port));

    *out_server_port = server_port;
    return ResultSuccess;
}

Result ServiceManager::UnregisterService(const std::string& name, u32 owner_process_id) {
    R_TRY(ValidateServiceName(name));
    const auto owner = service_owners.find(name);
    R_UNLESS(owner != service_owners.end(), ResultServiceNotRegistered);
    R_UNLESS(owner->second == owner_process_id, ResultAccessDenied);

    const auto service = registered_services.find(name);
    if (service != registered_services.end()) {
        registered_services_inverse.erase(service->second->GetObjectId());
        registered_services.erase(service);
    }
    service_owners.erase(owner);
    return ResultSuccess;
}

void ServiceManager::UnregisterServicesForProcess(u32 owner_process_id) {
    if (owner_process_id == 0) {
        return;
    }
    for (auto owner = service_owners.begin(); owner != service_owners.end();) {
        if (owner->second != owner_process_id) {
            ++owner;
            continue;
        }
        const auto service = registered_services.find(owner->first);
        if (service != registered_services.end()) {
            registered_services_inverse.erase(service->second->GetObjectId());
            registered_services.erase(service);
        }
        owner = service_owners.erase(owner);
    }
}

Result ServiceManager::GetServicePort(std::shared_ptr<Kernel::ClientPort>* out_client_port,
                                      const std::string& name) {
    R_TRY(ValidateServiceName(name));

    auto it = registered_services.find(name);
    R_UNLESS(it != registered_services.end(), ResultServiceNotRegistered);

    *out_client_port = it->second;
    return ResultSuccess;
}

Result ServiceManager::ConnectToService(std::shared_ptr<Kernel::ClientSession>* out_client_session,
                                        const std::string& name) {
    std::shared_ptr<Kernel::ClientPort> client_port;
    R_TRY(GetServicePort(std::addressof(client_port), name));
    return client_port->Connect(out_client_session);
}

std::string ServiceManager::GetServiceNameByPortId(u32 port) const {
    if (registered_services_inverse.count(port)) {
        return registered_services_inverse.at(port);
    }

    return "";
}

} // namespace Service::SM
