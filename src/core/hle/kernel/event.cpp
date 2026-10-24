// Copyright 2014-2024 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/serialization/base_object.hpp>
#include <boost/serialization/string.hpp>
#include "common/archives.h"
#include "common/assert.h"
#include "core/core.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/level256_diagnostic.h"
#include "core/hle/kernel/resource_limit.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/service/plgldr/plgldr.h"
#include "core/hle/service/plgldr/plugin_path.h"

SERIALIZE_EXPORT_IMPL(Kernel::Event)

namespace Kernel {

Event::Event(KernelSystem& kernel) : WaitObject(kernel) {}

Event::~Event() {
    if (resource_limit) {
        resource_limit->Release(ResourceLimitType::Event, 1);
    }
}

std::shared_ptr<Event> KernelSystem::CreateEvent(ResetType reset_type, std::string name) {
    auto event = std::make_shared<Event>(*this);
    event->signaled = false;
    event->reset_type = reset_type;
    event->name = std::move(name);
    return event;
}

bool Event::ShouldWait(const Thread* thread) const {
    return !signaled;
}

void Event::Acquire(Thread* thread) {
    ASSERT_MSG(!ShouldWait(thread), "object unavailable!");

    if (reset_type == ResetType::OneShot) {
        signaled = false;
    }
}

void Event::Signal() {
    // Count one notification when this kernel Event is actually associated with a waiting
    // thread owned by the hash-verified Level256 plugin. Guest LightEvents are observed through
    // their real arbiter address in the SVC diagnostic instead.
    for (const auto& thread : GetWaitingThreads()) {
        if (auto process = thread->owner_process.lock()) {
            auto& system = Core::Global<Core::System>();
            if (auto plugin_loader = Service::PLGLDR::GetService(system)) {
                const auto& context = plugin_loader->GetPluginLoaderContext();
                if (!context.plugin_loaded || !context.level256_plugin_identity_verified ||
                    context.plugin_process_id != process->process_id || !process->codeset ||
                    !Service::PLGLDR::IsLevel256SupportedTitle(process->codeset->program_id)) {
                    continue;
                }
                plugin_loader->TraceLevel256GuestWait(*process, thread->GetThreadId(),
                                                      thread->context.GetProgramCounter(), "notify",
                                                      GetTypeName(), GetObjectId(), GetName(), 0);
                break;
            }
        }
    }
    signaled = true;
    WakeupAllWaitingThreads();
}

void Event::Clear() {
    signaled = false;
}

void Event::WakeupAllWaitingThreads() {
    WaitObject::WakeupAllWaitingThreads();

    if (reset_type == ResetType::Pulse) {
        signaled = false;
    }
}

template <class Archive>
void Event::serialize(Archive& ar, const unsigned int file_version) {
    ar& boost::serialization::base_object<WaitObject>(*this);
    ar & reset_type;
    ar & signaled;
    ar & name;
    ar & resource_limit;
    if (file_version >= 1) {
        ar & dsp_audio_irq_registered;
    } else if constexpr (Archive::is_loading::value) {
        dsp_audio_irq_registered = false;
    }
}
SERIALIZE_IMPL(Event)

} // namespace Kernel
