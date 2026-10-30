// Small services that only need to answer "yes, fine": parental controls, add-on content, logging, power.
#include <cstdlib>
#include "ipc.h"

namespace nx {

namespace {

std::shared_ptr<Service> make(const char* name, std::function<void(Service&)> setup = {}) {
    auto s = std::make_shared<Service>(name);
    if (setup) setup(*s);
    return s;
}

std::shared_ptr<Service> parental_control() {
    return make("IParentalControlService", [](Service& s) {
        s.cmds[1403] = [](IpcCtx& c) { c.push<uint8_t>(0); return 0u; };  // IsRestrictionEnabled
        s.cmds[1001] = [](IpcCtx&) { return 0u; };                          // CheckFreeCommunicationPermission
    });
}

void pctl(Service& s) {
    s.cmds[0] = [](IpcCtx& c) { c.push_object(parental_control()); return 0u; };  // CreateService
    s.cmds[1] = [](IpcCtx& c) { c.push_object(parental_control()); return 0u; };  // CreateServiceWithoutInitialize
}

ServiceRegistrar r_pctl("pctl", [] { return make("pctl", pctl); });
ServiceRegistrar r_pctl_s("pctl:s", [] { return make("pctl:s", pctl); });
ServiceRegistrar r_pctl_a("pctl:a", [] { return make("pctl:a", pctl); });
ServiceRegistrar r_pctl_r("pctl:r", [] { return make("pctl:r", pctl); });

ServiceRegistrar r_lm("lm", [] {
    return make("lm", [](Service& s) {
        s.cmds[0] = [](IpcCtx& c) { c.push_object(make("ILogger")); return 0u; };  // OpenLogger
    });
});

ServiceRegistrar r_apm("apm", [] {
    return make("apm", [](Service& s) {
        s.cmds[0] = [](IpcCtx& c) { c.push_object(make("ISession")); return 0u; };  // OpenSession
        s.cmds[1] = [](IpcCtx& c) {  // GetPerformanceMode: boost (docked), normal with NX_HANDHELD (svc_am.cpp)
            c.push<uint32_t>(std::getenv("NX_HANDHELD") ? 0 : 1);
            return 0u;
        };
    });
});

ServiceRegistrar r_aoc("aoc:u", [] {
    return make("aoc:u", [](Service& s) {
        s.cmds[2] = [](IpcCtx& c) { c.push<uint32_t>(0); return 0u; };  // CountAddOnContent (old)
        s.cmds[3] = [](IpcCtx& c) { c.push<uint32_t>(0); return 0u; };  // ListAddOnContent (old)
        s.cmds[7] = [](IpcCtx& c) { c.push<uint32_t>(0); return 0u; };
        s.cmds[8] = [](IpcCtx& c) {                                       // GetAddOnContentListChangedEvent
            auto e = std::make_shared<KEvent>();
            e->state = std::make_shared<EventState>();
            c.out_copy.push_back(handle_add(e));
            return 0u;
        };
    });
});

// amiibo (1.2.0 calls nn::nfp::Initialize at boot): an NFC stack with no reader attached.
uint32_t push_event(IpcCtx& c) {
    auto e = std::make_shared<KEvent>();
    e->state = std::make_shared<EventState>();
    c.out_copy.push_back(handle_add(e));
    return 0u;
}

std::shared_ptr<Service> nfc_user(const char* name) {
    return make(name, [](Service& s) {
        auto state = std::make_shared<uint32_t>(0);
        s.cmds[0] = [state](IpcCtx&) { *state = 1; return 0u; };                  // Initialize
        s.cmds[1] = [state](IpcCtx&) { *state = 0; return 0u; };                  // Finalize
        s.cmds[2] = [](IpcCtx& c) { c.push<uint32_t>(0); return 0u; };            // ListDevices: none
        s.cmds[17] = push_event;                                                   // AttachActivateEvent
        s.cmds[18] = push_event;                                                   // AttachDeactivateEvent
        s.cmds[19] = [state](IpcCtx& c) { c.push<uint32_t>(*state); return 0u; }; // GetState
        s.cmds[23] = push_event;                                                   // AttachAvailabilityChangeEvent
    });
}

ServiceRegistrar r_nfp_user("nfp:user", [] {
    return make("nfp:user", [](Service& s) {
        s.cmds[0] = [](IpcCtx& c) { c.push_object(nfc_user("nfp:IUser")); return 0u; };  // CreateUserInterface
    });
});

ServiceRegistrar r_nfc_user("nfc:user", [] {
    return make("nfc:user", [](Service& s) {
        s.cmds[0] = [](IpcCtx& c) { c.push_object(nfc_user("nfc:IUser")); return 0u; };  // CreateUserInterface
    });
});

}  // namespace

}  // namespace nx
