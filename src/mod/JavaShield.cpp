#include "mod/JavaShield.h"
#include "sdk/Actor.h"
#include "sdk/InteractionResult.h"
#include "sdk/ClientInstance.h"
#include "sdk/GameMode.h"
#include "sdk/TouchGlyphButtonControl.h"
#include <pl/Mod.hpp>
#include <pl/Input.hpp>
#include "sdk/Logger.h"
#include "sdk/SynchedActorDataAccess.h"
#include "sdk/ContainerScreenContext.h"

static ClientInstance* g_clientInstance = nullptr;

std::atomic_bool gTouchToggle{false};
std::atomic_bool gMouseRightDown{false};
std::atomic_bool gMouseCallbackRegistered{false};
thread_local int gShieldUpdateDepth = 0;

void registerMouseOnce() {
    bool expected = false;
    if (!gMouseCallbackRegistered.compare_exchange_strong(expected, true)) return;
    pl::input::registerMouseCallback([](const pl::input::MouseEvent& event) -> bool {
        if (event.button == 2) {
            gMouseRightDown.store(event.isDown, std::memory_order_relaxed);
        }
        return false;
    });
    gMouseCallbackRegistered.store(true, std::memory_order_relaxed);
}

bool requestedBlock() noexcept {
    if (g_clientInstance) {
        Player* localPlayer = g_clientInstance->getLocalPlayer();
        // Safe Java Feature Fix: Query riding state through metadata maps to satisfy missing SDK structures
        if (localPlayer) {
            bool isRidingEntity = SynchedActorDataAccess::getActorFlag(localPlayer->entityContext, ActorFlags::Riding);
            if (isRidingEntity) {
                return false;
            }
        }
    }
    return (gTouchToggle.load(std::memory_order_relaxed) || gMouseRightDown.load(std::memory_order_relaxed));
}

bool strcontains(const std::string& text, const std::string& search) {
    return text.find(search) != std::string::npos;
}

LL_TYPED_HOOK(
    GameMode_useItemOn,
    memory::HookPriority::Normal,
    GameMode,
    pl::memory::resolveVtableFunction("8GameMode", 14, "libminecraftpe.so"),
    "libminecraftpe.so",
    InteractionResult,
    ItemStackBase* stack,
    void* a3,
    unsigned char a4,
    void* a5,
    void* a6,
    bool a7
) {
    InteractionResult res = origin(stack, a3, a4, a5, a6, a7);
    if(!stack) return res;
    Item* item = stack->getItem();
    if(item) {
        if((item->mNamespace == "minecraft" && strcontains(item->getFullName(), "spear")) || (item->getFullName() != "minecraft:shield" && res.mSuccess)) gTouchToggle.store(false, std::memory_order_relaxed);
    }

    return res;
};

LL_TYPED_HOOK(
    Item_Item,
    memory::HookPriority::Normal,
    Item,
    "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D5 ? ? ? ? ? ? ? 91 ? ? ? F9 F5 03 02 2A",
    "libminecraftpe.so",
    void,
    std::string const& name, short id
) {
    setAllowOffhand(true);
    origin(name, id);
    setAllowOffhand(true);
};

LL_TYPED_HOOK(
    CreatedOutputContainerValidation_isItemAllowed,
    memory::HookPriority::Normal,
    GameMode,
    pl::memory::resolveVtableFunction("32CreatedOutputContainerValidation", 3, "libminecraftpe.so"),
    "libminecraftpe.so",
    bool,
    ContainerScreenContext* screenContext,
    int const                       slot,
    ItemStackBase*          item,
    int const                       amount,
    bool idk
) {
    if(!screenContext) return origin(screenContext, slot, item, amount, idk);

    if(slot == 1 && screenContext->mScreenContainerType == SharedTypes::Legacy::ContainerType::Hand) return false;
    return origin(screenContext, slot, item, amount, idk);
};

LL_TYPED_HOOK(
    GameMode_useItem,
    memory::HookPriority::Normal,
    GameMode,
    pl::memory::resolveVtableFunction("8GameMode", 12, "libminecraftpe.so"),
    "libminecraftpe.so",
    bool,
    ItemStackBase* stack
) {
    bool res = origin(stack);
    if(!stack) return res;
    Item* item = stack->getItem();
    if(item) {
        if((item->mNamespace == "minecraft" && strcontains(item->getFullName(), "spear")) || (item->getFullName() != "minecraft:shield" && res)) gTouchToggle.store(false, std::memory_order_relaxed);
    }

    return res;
};

LL_INSTANCE_HOOK(
    Shield_update,
    memory::HookPriority::Normal,
    "FF ?? 03 D1 FD 7B ?? A9 F9 53 00 F9 F8 5F ?? A9 F6 57 ?? A9 F4 4F ?? A9 FD 43 02 91 58 D0 3B D5 F3 03 00 AA 00 20 00 91",
    "libminecraftpe.so",
    void
) {
    ++gShieldUpdateDepth;
    origin();
    --gShieldUpdateDepth;
};

LL_TYPED_HOOK(
    Actor_isSneaking,
    memory::HookPriority::Normal,
    Actor,
    "00 20 00 91 21 00 80 52 ?? ?? ?? 14 08 00 40 39 3F 00 08 6A E0 07 9F 1A C0 03 5F D6",
    "libminecraftpe.so",
    bool
) {
    if(g_clientInstance) {
        if(g_clientInstance->getScreenName() != "hud_screen") gTouchToggle.store(false, std::memory_order_relaxed);
        Player* localPlayer = g_clientInstance->getLocalPlayer();
        if(localPlayer) {
            ItemStackBase* mainhandItemStack = localPlayer->getHand(0);
            if(mainhandItemStack) {
                Item* mainhandItem = mainhandItemStack->getItem();
                if(mainhandItem) {
                    if(mainhandItem->mNamespace == "minecraft" && strcontains(mainhandItem->getFullName(), "spear")) gTouchToggle.store(false, std::memory_order_relaxed);
                }
            }
            
            bool isBlocking = requestedBlock();
            SynchedActorDataAccess::setActorFlag(localPlayer->entityContext, ActorFlags::Blocking, isBlocking);
            
            // Java Feature: Stop sprinting and force slowdown when blocking with a shield
            if (isBlocking) {
                SynchedActorDataAccess::setActorFlag(localPlayer->entityContext, ActorFlags::Sprinting, false);
            }
        }
    }
    if (gShieldUpdateDepth > 0) {
        return requestedBlock();
    }
    return origin();
};

LL_TYPED_HOOK(
    ClientInstance_update,
    memory::HookPriority::Normal,
    ClientInstance,
    pl::memory::resolveVtableFunction("14ClientInstance", 25, "libminecraftpe.so"),
    "libminecraftpe.so",
    bool, bool isInitFinished
) {
    if(isInitFinished && !g_clientInstance) g_clientInstance = (ClientInstance*)this;
    return origin(isInitFinished);
};

LL_TYPED_HOOK(
    TouchGlyphButtonControl_tick,
    memory::HookPriority::Normal,
    TouchGlyphButtonControl,
    pl::memory::resolveVtableFunction("23TouchGlyphButtonControl", 6, "libminecraftpe.so"),
    "libminecraftpe.so",
    void, void* a1, void* a2, void* a3
) {
    uint8_t oldState = mState; 
    origin(a1, a2, a3);
    if(mId == 0x8A5001FC) {
        if(mState) {
            gTouchToggle.store(true, std::memory_order_relaxed);
        } else {
            gTouchToggle.store(false, std::memory_order_relaxed);
        }
    }
};

namespace nexcaise {

JavaShield &JavaShield::getInstance() {
    static JavaShield instance;
    return instance;
}

JavaShield::JavaShield() : mSelf(*ll::mod::NativeMod::current()) {}

bool JavaShield::load() {
    auto &self = getSelf();
    self.getLogger().info("Loading...");
    registerMouseOnce();
    return true;
}

bool JavaShield::enable() {
    auto &self = getSelf();
    self.getLogger().info("Enabling...");

    TouchGlyphButtonControl_tick::hook();
    ClientInstance_update::hook();
    Shield_update::hook();
    Actor_isSneaking::hook();
    GameMode_useItemOn::hook();
    GameMode_useItem::hook();
    Item_Item::hook();
    CreatedOutputContainerValidation_isItemAllowed::hook();

    return true;
}

bool JavaShield::disable() {
    getSelf().getLogger().info("Disabling...");

    TouchGlyphButtonControl_tick::unhook();
    ClientInstance_update::unhook();
    Shield_update::unhook();
    Actor_isSneaking::unhook();
    GameMode_useItemOn::unhook();
    GameMode_useItem::unhook();
    Item_Item::unhook();
    CreatedOutputContainerValidation_isItemAllowed::unhook();

    gTouchToggle.store(false, std::memory_order_relaxed);
    gMouseRightDown.store(false, std::memory_order_relaxed);
    gMouseCallbackRegistered.store(false, std::memory_order_relaxed);

    return true;
}

bool JavaShield::unload() {
    getSelf().getLogger().info("Unloading...");
    return true;
}

} // namespace nexcaise
