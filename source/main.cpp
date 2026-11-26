#include <thread>
#include "switch.h"
#include "util/debug.h"
#include "util/error.hpp"
#include "ui/MainApplication.hpp"
#include "util/util.hpp"
#include "util/config.hpp"
#include "util/nexus_update.hpp"
#include "util/offline_db_update.hpp"

using namespace pu::ui::render;
int main(int argc, char* argv[])
{
    bool appInitialized = false;
    try {
        debugLogReset();
        inst::util::initApp();
        appInitialized = true;
        auto renderer = Renderer::New(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER,
            RendererInitOptions::RendererNoSound, RendererHardwareFlags);
        auto main = inst::ui::MainApplication::New(renderer);
        inst::update::setLaunchPath(argc ? argv[0] : nullptr);
        std::thread offlineDbUpdateCheckThread;
        inst::offline::dbupdate::ResetStartupCheckState();
        if (inst::config::offlineDbAutoCheckOnStartup && inst::util::getIPAddress() != "1.0.0.127") {
            offlineDbUpdateCheckThread = std::thread([]() {
                const auto result = inst::offline::dbupdate::CheckForUpdate(inst::config::offlineDbManifestUrl);
                inst::offline::dbupdate::SetStartupCheckResult(result);
            });
        }
        main->Prepare();
        if (inst::config::autoUpdate) inst::update::startBootCheck();
        main->ShowWithFadeIn();
        inst::update::finishBootCheck();
        if (offlineDbUpdateCheckThread.joinable()) {
            offlineDbUpdateCheckThread.join();
        }
    } catch (std::exception& e) {
        LOG_DEBUG("An error occurred:\n%s", e.what());
    } catch (...) {
        LOG_DEBUG("An unknown error occurred during startup.");
    }
    if (appInitialized) {
        inst::util::deinitApp();
        try {
            inst::update::activatePending();
        } catch (const std::exception& error) {
            ErrorApplicationConfig dialog{};
            if (R_SUCCEEDED(errorApplicationCreate(&dialog, "CyberFoil Nexus update could not finish", error.what())))
                errorApplicationShow(&dialog);
        }
    }
    return 0;
}
