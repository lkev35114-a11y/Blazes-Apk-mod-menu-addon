// Prisma Client V2: a hand menu (forked from the starter menu) in the style of ii's Stupid Menu (Gorilla Tag).
// The layout idea comes from ii's menu; the code is new.
//   Hold Y (left hand)      -> menu on your left hand, tipped like ii's; turn your palm to you
//                              (B and the right hand with "Right Hand Menu")
//   Touch a button with your other index finger to press it
//   < and > on the sides    -> previous / next page
//   Pages: Movement Mods, Rig Mods, Items, Settings, Enabled Mods
// Movement only changes your own game. Rig mods (dances) and Items are seen by the room,
// within the limits in AGENTS.md ("Things others see"). Read docs/GUIDE.md before adding.
#include "ymsdk.hpp"
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <dirent.h>
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

using namespace BNM;
using namespace BNM::Structures::Unity;
using BNM::IL2CPP::Il2CppObject;

YEEPS_MOD("Prisma Client V2")

// ---- small helpers ---------------------------------------------------------

static std::string TypeName(const BNM::IL2CPP::Il2CppType *t) {
    Class c(t);
    if (!c.IsValid() || !c.GetClass() || !c.GetClass()->name) return "?";
    auto *k = c.GetClass();
    return (k->namespaze && k->namespaze[0]) ? std::string(k->namespaze) + "." + k->name : k->name;
}
// Pick an overload by its exact parameter types, e.g. GetComponent(System.Type).
static MethodBase Overload(Class c, const char *name, std::vector<std::string> types) {
    for (auto &m : c.GetMethods(false)) {
        auto *mi = m.GetInfo();
        if (!mi || !mi->name || strcmp(mi->name, name) != 0 || mi->parameters_count != types.size()) continue;
        bool ok = true;
        for (size_t i = 0; i < types.size() && ok; i++) ok = TypeName(mi->parameters[i]) == types[i];
        if (ok) return m;
    }
    return {};
}

// Unity objects we keep between frames must be held by a GC handle, or the
// garbage collector can free them under us.
static void *(*p_handleNew)(void *, bool);
static void *(*p_handleTarget)(void *);
static void (*p_handleFree)(void *);
struct Ref {
    void *h = nullptr;
    void set(Il2CppObject *o) { clear(); if (o && p_handleNew) h = p_handleNew(o, false); }
    Il2CppObject *get() const { return h && p_handleTarget ? (Il2CppObject *)p_handleTarget(h) : nullptr; }
    void clear() { if (h && p_handleFree) p_handleFree(h); h = nullptr; }
};

static Color Lerp(Color a, Color b, float t) {
    return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1);
}
static Color Rainbow(float h) {             // hue 0..1 -> bright color
    h = h - floorf(h);
    float r = fabsf(h * 6 - 3) - 1, g = 2 - fabsf(h * 6 - 2), b = 2 - fabsf(h * 6 - 4);
    auto c = [](float v) { return v < 0 ? 0.f : v > 1 ? 1.f : v; };
    return Color(c(r), c(g), c(b), 1);
}

// ---- game lookups, resolved once -------------------------------------------

struct XRDevice { uint64_t id; bool initialized; };
static struct {
    bool ok = false;
    Method<Il2CppObject *> createPrimitive, goTransform, goGetComponent, goAddComponent, compTransform, compGameObject;
    Method<void> goSetActive, destroy, trSetParent, trSetPosition, trSetRotation, trSetLocalPosition,
                 trSetLocalRotation, trSetLocalScale, rbSetVelocity, matSetColor, matSetShader,
                 textSetFont, textSetText, textSetFontSize, textSetAlignment, graphicSetColor,
                 canvasSetRenderMode, rtSetSizeDelta, textSetFontStyle, textSetBestFit, textSetMinSize,
                 textSetMaxSize, teleportTo, rbAddForce, rbSetUseGravity, colliderSetEnabled,
                 lrSetPositionCount, lrSetPosition, lrSetStartWidth, lrSetEndWidth, lrSetUseWorldSpace;
    Method<Quaternion> trGetRotation;
    Method<Vector3> trGetPosition, trGetForward, trGetRight, trGetUp, trInverseTransformPoint, trTransformPoint, rbGetVelocity;
    Method<bool> alive, tryGetBool, haptic, raycast, tryGetVec3, tryGetQuat;
    Method<XRDevice> deviceAtNode;
    Method<Il2CppObject *> trGetParent, trGetRoot, getComponentsInChildren, netLocal;
    Method<Il2CppObject *> overlapSphere, colAttachedRb;
    Method<void> matSetInt, rsSetFog, rsSetAmbientMode, rsSetAmbientLight;
    Method<bool> rsGetFog;
    Method<int> rsGetAmbientMode;
    Method<Color> rsGetAmbientLight;
    BNM::MonoType *tJoint = nullptr;
    Method<BNM::Structures::Mono::String *> objGetName;
    Method<void> pvRpc;
    BNM::IL2CPP::MethodInfo *rpcCreateItem = nullptr;     // only read for its parameter types
    BNM::IL2CPP::Il2CppClass *objectClass = nullptr, *stringClass = nullptr;
    BNM::MonoType *tPhotonView = nullptr, *tItem = nullptr;
    Method<BNM::Structures::Mono::String *> itemKey, itemId;
    Method<bool> itemHeld;
    Method<void> itemDrop, setHeldItem;
    void *(*arrayNew)(void *, uintptr_t) = nullptr;
    void *(*valueBox)(void *, void *) = nullptr;
    Method<bool> behaviourGetEnabled;
    Method<void> behaviourSetEnabled;
    std::vector<BNM::MonoType *> trackerTypes;
    Method<Il2CppObject *> rendererGetMaterial, shaderFind, findAll, findScene, findSceneByType, builtinResource;
    Field<Il2CppObject *> leftHand, rightHand, headTransform, bodyCollider, headCollider;
    Field<float> maxJumpSpeed, jumpMultiplier;
    Method<Vector3> plGetVelocity;
    Method<void> plSetVelocity, plNoGravityFor;
    Field<int> locomotionLayers, collisionLayers;
    Method<int> goGetLayer;
    Method<bool> colliderIsTrigger, behaviourGetEnabledCollider;
    Method<void> ignoreLayers;            // a LayerMask: which layers your hands can climb on
    Method<void> goSetLayer;
    BNM::MonoType *tRigidbody = nullptr, *tCollider = nullptr, *tCanvas = nullptr, *tText = nullptr,
                  *tFont = nullptr, *tRenderer = nullptr, *tMeshCollider = nullptr, *tLine = nullptr;
    Ref shader, font, usagePrimary, usageSecondary, usageGrip, usageTrigger;
} G;

static bool Lookups() {
    if (G.ok) return true;
    auto R = ym::api()->resolve;
    p_handleNew = (decltype(p_handleNew))R("il2cpp_gchandle_new");
    p_handleTarget = (decltype(p_handleTarget))R("il2cpp_gchandle_get_target");
    p_handleFree = (decltype(p_handleFree))R("il2cpp_gchandle_free");
    Class go("UnityEngine", "GameObject"), tr("UnityEngine", "Transform"), obj("UnityEngine", "Object"),
          rb("UnityEngine", "Rigidbody"), mat("UnityEngine", "Material"), rend("UnityEngine", "Renderer"),
          sh("UnityEngine", "Shader"), res("UnityEngine", "Resources"), text("UnityEngine.UI", "Text"),
          graphic("UnityEngine.UI", "Graphic"), canvas("UnityEngine", "Canvas"), rt("UnityEngine", "RectTransform"),
          dev("UnityEngine.XR", "InputDevices"), player("", "MasterPlayer", Image("Assembly-CSharp.dll"));
    if (!go || !tr || !player || !dev) { ym::log("prisma: game classes missing"); return false; }
    G.createPrimitive = go.GetMethod("CreatePrimitive", 1);
    G.goTransform = go.GetMethod("get_transform", 0);
    G.goGetComponent = Overload(go, "GetComponent", {"System.Type"});
    G.goAddComponent = Overload(go, "AddComponent", {"System.Type"});
    G.goSetActive = go.GetMethod("SetActive", 1);
    Class comp("UnityEngine", "Component");
    G.compTransform = comp.GetMethod("get_transform", 0);
    G.compGameObject = comp.GetMethod("get_gameObject", 0);
    G.destroy = Overload(obj, "Destroy", {"UnityEngine.Object"});
    G.alive = obj.GetMethod("op_Implicit", 1);
    G.trSetParent = Overload(tr, "SetParent", {"UnityEngine.Transform", "System.Boolean"});
    G.trSetPosition = tr.GetMethod("set_position", 1);
    G.trSetRotation = tr.GetMethod("set_rotation", 1);
    G.trSetLocalPosition = tr.GetMethod("set_localPosition", 1);
    G.trSetLocalRotation = tr.GetMethod("set_localRotation", 1);
    G.trSetLocalScale = tr.GetMethod("set_localScale", 1);
    G.trGetPosition = tr.GetMethod("get_position", 0);
    G.trGetRotation = tr.GetMethod("get_rotation", 0);
    G.trGetForward = tr.GetMethod("get_forward", 0);
    G.trGetRight = tr.GetMethod("get_right", 0);
    G.trGetUp = tr.GetMethod("get_up", 0);
    G.trInverseTransformPoint = Overload(tr, "InverseTransformPoint", {"UnityEngine.Vector3"});
    G.trTransformPoint = Overload(tr, "TransformPoint", {"UnityEngine.Vector3"});
    G.rbSetVelocity = rb.GetMethod("set_velocity", 1);
    G.rbGetVelocity = rb.GetMethod("get_velocity", 0);
    G.rendererGetMaterial = rend.GetMethod("get_material", 0);
    G.matSetColor = Overload(mat, "set_color", {"UnityEngine.Color"});
    G.matSetShader = mat.GetMethod("set_shader", 1);
    G.shaderFind = sh.GetMethod("Find", 1);
    G.findAll = Overload(res, "FindObjectsOfTypeAll", {"System.Type"});
    G.textSetFont = text.GetMethod("set_font", 1);
    G.textSetFontStyle = text.GetMethod("set_fontStyle", 1);
    G.textSetBestFit = text.GetMethod("set_resizeTextForBestFit", 1);
    G.textSetMinSize = text.GetMethod("set_resizeTextMinSize", 1);
    G.textSetMaxSize = text.GetMethod("set_resizeTextMaxSize", 1);
    G.textSetText = text.GetMethod("set_text", 1);
    G.textSetFontSize = text.GetMethod("set_fontSize", 1);
    G.textSetAlignment = text.GetMethod("set_alignment", 1);
    G.graphicSetColor = graphic.GetMethod("set_color", 1);
    G.canvasSetRenderMode = canvas.GetMethod("set_renderMode", 1);
    G.rtSetSizeDelta = rt.GetMethod("set_sizeDelta", 1);
    G.deviceAtNode = dev.GetMethod("GetDeviceAtXRNode", 1);
    G.tryGetBool = dev.GetMethod("TryGetFeatureValue_bool", 3);
    G.haptic = dev.GetMethod("SendHapticImpulse", 4);    // optional: a small buzz on press
    G.leftHand = player.GetField("leftHandTransform");
    G.rightHand = player.GetField("rightHandTransform");
    G.headTransform = player.GetField("headTransform");
    G.bodyCollider = player.GetField("bodyCollider");
    G.headCollider = player.GetField("headCollider");
    G.maxJumpSpeed = player.GetField("maxJumpSpeed");
    G.locomotionLayers = player.GetField("locomotionEnabledLayers");
    G.collisionLayers = player.GetField("collisionEnabledLayers");
    G.goGetLayer = go.GetMethod("get_layer", 0);
    G.ignoreLayers = Class("UnityEngine", "Physics").GetMethod("IgnoreLayerCollision", 3);
    // Yeeps moves your body through these, not through a Rigidbody on the player object
    G.plGetVelocity = player.GetMethod("GetVelocity", 0);
    G.plSetVelocity = player.GetMethod("ApplySetVelocityNoHaptic", 1);
    if (!G.plSetVelocity.IsValid()) G.plSetVelocity = player.GetMethod("ApplySetVelocity", 1);
    G.plNoGravityFor = player.GetMethod("ApplyNoGravityDuration", 1);
    ym::log("prisma: velocity %d/%d, no-gravity %d", G.plGetVelocity.IsValid(), G.plSetVelocity.IsValid(),
            G.plNoGravityFor.IsValid());
    G.goSetLayer = go.GetMethod("set_layer", 1);
    G.jumpMultiplier = player.GetField("jumpMultiplier");
    G.teleportTo = player.GetMethod("TeleportTo", 1);
    G.rbAddForce = Overload(rb, "AddForce", {"UnityEngine.Vector3", "UnityEngine.ForceMode"});
    G.rbSetUseGravity = rb.GetMethod("set_useGravity", 1);
    G.colliderSetEnabled = Class("UnityEngine", "Collider").GetMethod("set_enabled", 1);
    G.behaviourGetEnabledCollider = Class("UnityEngine", "Collider").GetMethod("get_enabled", 0);
    G.colliderIsTrigger = Class("UnityEngine", "Collider").GetMethod("get_isTrigger", 0);
    G.raycast = Overload(Class("UnityEngine", "Physics"), "Raycast",
                         {"UnityEngine.Vector3", "UnityEngine.Vector3", "UnityEngine.RaycastHit", "System.Single", "System.Int32"});
    G.findScene = Overload(obj, "FindObjectsOfType", {"System.Type"});      // only when noclip starts
    G.findSceneByType = Overload(obj, "FindObjectsByType", {"System.Type", "UnityEngine.FindObjectsSortMode"});
    G.builtinResource = Overload(res, "GetBuiltinResource", {"System.Type", "System.String"});
    Class line("UnityEngine", "LineRenderer");
    G.tLine = line.GetMonoType();
    G.lrSetPositionCount = line.GetMethod("set_positionCount", 1);
    G.lrSetPosition = line.GetMethod("SetPosition", 2);
    G.lrSetStartWidth = line.GetMethod("set_startWidth", 1);
    G.lrSetEndWidth = line.GetMethod("set_endWidth", 1);
    G.lrSetUseWorldSpace = line.GetMethod("set_useWorldSpace", 1);
    G.tMeshCollider = Class("UnityEngine", "MeshCollider").GetMonoType();
    G.trGetParent = tr.GetMethod("get_parent", 0);
    G.trGetRoot = tr.GetMethod("get_root", 0);
    G.getComponentsInChildren = Overload(go, "GetComponentsInChildren", {"System.Type", "System.Boolean"});
    G.overlapSphere = Overload(Class("UnityEngine", "Physics"), "OverlapSphere", {"UnityEngine.Vector3", "System.Single"});
    G.objGetName = obj.GetMethod("get_name", 0);
    G.colAttachedRb = Class("UnityEngine", "Collider").GetMethod("get_attachedRigidbody", 0);
    { Class jt("UnityEngine", "Joint"); G.tJoint = jt.IsValid() ? jt.GetMonoType() : nullptr; }
    G.tryGetVec3 = dev.GetMethod("TryGetFeatureValue_Vector3", 3);
    G.tryGetQuat = dev.GetMethod("TryGetFeatureValue_Quaternion", 3);
    G.matSetInt = Overload(mat, "SetInt", {"System.String", "System.Int32"});
    {
        Class rs("UnityEngine", "RenderSettings");
        G.rsSetFog = rs.GetMethod("set_fog", 1);
        G.rsGetFog = rs.GetMethod("get_fog", 0);
        G.rsSetAmbientMode = rs.GetMethod("set_ambientMode", 1);
        G.rsGetAmbientMode = rs.GetMethod("get_ambientMode", 0);
        G.rsSetAmbientLight = rs.GetMethod("set_ambientLight", 1);
        G.rsGetAmbientLight = rs.GetMethod("get_ambientLight", 0);
    }
    Class net("", "NetworkMessenger", Image("Assembly-CSharp.dll"));
    if (net.IsValid()) {
        G.netLocal = net.GetMethod("get_local", 0);
        for (auto &m : net.GetMethods(false))
            if (m.GetInfo() && m.GetInfo()->name && strcmp(m.GetInfo()->name, "RpcCreateItem") == 0) G.rpcCreateItem = m.GetInfo();
    }
    {
        Class pv("Photon.Pun", "PhotonView");
        G.tPhotonView = pv.IsValid() ? pv.GetMonoType() : nullptr;
        if (pv.IsValid()) G.pvRpc = Overload(pv, "RPC", {"System.String", "Photon.Pun.RpcTarget", "System.Object[]"});
        Class item("", "Item", Image("Assembly-CSharp.dll"));
        if (item.IsValid()) {
            G.tItem = item.GetMonoType();
            G.itemKey = item.GetMethod("get_itemKey", 0);
            G.itemHeld = item.GetMethod("IsHeldOrAnchoredToAnyPlayer", 0);
            G.itemId = item.GetMethod("get_itemID", 0);
            G.itemDrop = item.GetMethod("TrySendDrop", 0);
            G.setHeldItem = Class("", "MasterPlayer", Image("Assembly-CSharp.dll")).GetMethod("SetHeldItem", 2);
        }
        G.objectClass = Class("System", "Object").GetClass();
        G.stringClass = Class("System", "String").GetClass();
        G.arrayNew = (decltype(G.arrayNew))ym::api()->resolve("il2cpp_array_new");
        G.valueBox = (decltype(G.valueBox))ym::api()->resolve("il2cpp_value_box");
        ym::log("prisma: items rpc=%d pv=%d item=%d box=%d", G.rpcCreateItem != nullptr, G.pvRpc.IsValid(),
                G.itemKey.IsValid(), G.arrayNew && G.valueBox);
    }
    Class behaviour("UnityEngine", "Behaviour");
    G.behaviourGetEnabled = behaviour.GetMethod("get_enabled", 0);
    G.behaviourSetEnabled = behaviour.GetMethod("set_enabled", 1);
    // What moves your hands with the controllers (paused while you dance)
    for (auto nc : std::vector<std::pair<const char *, const char *>>{
             {"UnityEngine.XR.Interaction.Toolkit", "XRBaseController"},
             {"UnityEngine.InputSystem.XR", "TrackedPoseDriver"},
             {"UnityEngine.SpatialTracking", "TrackedPoseDriver"}}) {
        Class c(nc.first, nc.second);
        if (c.IsValid()) G.trackerTypes.push_back(c.GetMonoType());
    }
    G.tRigidbody = rb.GetMonoType();
    G.tCollider = Class("UnityEngine", "Collider").GetMonoType();
    G.tRenderer = rend.GetMonoType();
    G.tCanvas = canvas.GetMonoType();
    G.tText = text.GetMonoType();
    G.tFont = Class("UnityEngine", "Font").GetMonoType();
    G.usagePrimary.set((Il2CppObject *)CreateMonoString("PrimaryButton"));
    G.usageSecondary.set((Il2CppObject *)CreateMonoString("SecondaryButton"));
    G.usageGrip.set((Il2CppObject *)CreateMonoString("GripButton"));
    G.usageTrigger.set((Il2CppObject *)CreateMonoString("TriggerButton"));
    for (const char *s : {"Universal Render Pipeline/Unlit", "Universal Render Pipeline/Lit"})
        if (!G.shader.get() && G.shaderFind.IsValid()) G.shader.set(G.shaderFind((BNM::Structures::Mono::String *)CreateMonoString(s)));
    // Arial, like ii's menu (Unity 6 calls it LegacyRuntime.ttf)
    for (const char *f : {"LegacyRuntime.ttf", "Arial.ttf"})
        if (!G.font.get() && G.builtinResource.IsValid() && G.tFont)
            G.font.set(G.builtinResource((Il2CppObject *)G.tFont, CreateMonoString(f)));
    if (!G.font.get() && G.findAll.IsValid() && G.tFont) {   // else any font the game loaded (one time)
        auto *arr = (uint8_t *)G.findAll((Il2CppObject *)G.tFont);
        if (arr && *(uintptr_t *)(arr + 0x18) > 0) G.font.set(((Il2CppObject **)(arr + 0x20))[0]);
    }
    G.ok = G.createPrimitive.IsValid() && G.goTransform.IsValid() && G.trSetPosition.IsValid() &&
           G.deviceAtNode.IsValid() && G.tryGetBool.IsValid() && G.leftHand.IsValid();
    if (!G.ok) ym::log("prisma: some game methods are missing, menu off");
    return G.ok;
}

static bool Alive(Il2CppObject *o) { return o && G.alive((Il2CppObject *)o); }
static Il2CppObject *Tr(Il2CppObject *gameObject) { return G.goTransform[gameObject](); }
static void Show(Ref &r, bool on) { if (Alive(r.get())) G.goSetActive[r.get()](on); }
static void Kill(Ref &r) { if (Alive(r.get())) G.destroy(r.get()); r.clear(); }

// A primitive with a flat color. collider=false removes its collider so it can't push you.
// Always give new objects the URP shader: Unity's default material only draws in one
// eye on Quest.
static Il2CppObject *Prim(int type, Color c, bool collider) {
    Il2CppObject *o = G.createPrimitive(type);   // 0 = Sphere, 3 = Cube
    if (!o) return nullptr;
    if (!collider) if (auto *col = G.goGetComponent[o](G.tCollider)) G.destroy(col);
    if (auto *r = G.goGetComponent[o](G.tRenderer)) {
        Il2CppObject *m = G.rendererGetMaterial[r]();
        if (m && G.shader.get()) G.matSetShader[m](G.shader.get());
        if (m) G.matSetColor[m](c);
    }
    return o;
}
static Il2CppObject *Cube(Color c, bool collider) { return Prim(3, c, collider); }
static void SetColor(Il2CppObject *o, Color c) {
    if (!Alive(o)) return;
    if (auto *r = G.goGetComponent[o](G.tRenderer)) if (Il2CppObject *m = G.rendererGetMaterial[r]()) G.matSetColor[m](c);
}
static void SetText(Ref &text, const char *s) {
    if (Alive(text.get())) G.textSetText[text.get()](CreateMonoString(s));
}

// ---- input -------------------------------------------------------------------

struct Buttons { bool a = false, b = false, x = false, y = false, lgrip = false, rgrip = false, ltrig = false, rtrig = false; };
static bool Pressed(int node, Ref &usage) {
    XRDevice d = G.deviceAtNode(node);
    bool v = false;
    if (d.initialized) G.tryGetBool(d.id, (BNM::Structures::Mono::String *)usage.get(), &v);
    return v;
}
static Buttons ReadButtons() {
    const int left = 4, right = 5;              // XRNode.LeftHand / RightHand
    Buttons b;
    b.x = Pressed(left, G.usagePrimary);  b.y = Pressed(left, G.usageSecondary);
    b.a = Pressed(right, G.usagePrimary); b.b = Pressed(right, G.usageSecondary);
    b.lgrip = Pressed(left, G.usageGrip); b.rgrip = Pressed(right, G.usageGrip);
    b.ltrig = Pressed(left, G.usageTrigger); b.rtrig = Pressed(right, G.usageTrigger);
    return b;
}

// ---- menu contents -------------------------------------------------------------

// Feature switches. Every feature only changes your own game.
static bool f_velocityFly, f_ironMan, f_tpGun, f_noclip, f_speedBoost, f_noGravity, f_longArms, f_rope;
static bool f_itemGun, f_spawnOnHand, f_car;
static bool f_esp, f_espTracers = true, f_noFog, f_bright, f_speedo;
static bool g_inSounds = false, g_armsTracker = false, g_trackerBad = false;
static float g_speedNow = 0;
static char g_itemKey[64] = "";                // the last item you held (learned on grab)
struct PendingItem { Vector3 pos, vel; };
static std::vector<PendingItem> g_itemQueue;
static double g_nextItem = 0, g_nextBatch = 0;
static int g_dupeArmed = 0;

static constexpr int kDances = 11;
static bool g_danceSel[kDances];
static int g_dance = -1;            // the picked dance, -1 = none
static bool f_fly, f_fastFly, f_platforms, f_lowGravity, f_rainbow, f_fps, f_clock, f_rightHand,
            f_notify = true, f_italic = true, f_outline = true, f_pop = true, f_layoutFloor = false;
static Ref g_player, g_rigidbody, g_leftHand, g_rightHand, g_head;
static Vector3 g_spot;
static bool g_haveSpot = false;

static void Notify(const char *fmt, ...);
static void SaveSpot();
static void GoToSpot();
static void NextTheme();
static void AllOff();
static void PointerToHand();
static void NextPointer();
static void NextIronSpeed();
static void NextCarSpeed();
static void SpeedBoostToggled();
static void NextBoost();
static void NextArm();
static void NextRopePower();
static void NextTitleStyle();
static void NextMenuSize();
static void NextPointerSize();
static void NextLayoutSize();
static void ApplyLook();
static void ResetMotionState();
static void NextArmMethod();
static void ScanSounds();
static void V2Reset();
static void ApplyGravity();
static void DancePicked();
static void ArmDupe9();
static void ArmDupe27();
static bool HeldItem();

// Item layouts (pictures made of the item in your hand). One function per layout.
#define LAYOUT_LIST(X) X(Tree) X(House) X(Heart) X(Smiley) X(Circle) X(Diamond) X(Pyramid) X(Plus) X(Wall) \
    X(RightArrow) X(LeftArrow) X(UpArrow) X(DownArrow) X(Hi) X(Hello)
enum LayoutId {
#define X(n) L_##n,
    LAYOUT_LIST(X)
#undef X
    L_COUNT
};
#define X(n) static void Lay##n();
LAYOUT_LIST(X)
#undef X

enum Cat { MAIN, MOVEMENT, RIG, ITEMS, LAYOUTS, SETTINGS, LOOK, SOUNDS, VISUALS, CREDITS, ENABLED, NCAT };
static const char *kCatName[NCAT] = {"Main", "Movement Mods", "Rig Mods", "Items", "Item Layouts", "Settings", "Menu Look", "Soundboard",
                                  "Visual Mods", "Credits", "Enabled Mods"};
enum Kind { OPEN, TOGGLE, ACTION };
struct Item {
    Cat cat;
    const char *label;
    Kind kind;
    Cat target = MAIN;            // OPEN: the page it opens
    bool *flag = nullptr;         // TOGGLE: the switch
    void (*fn)() = nullptr;       // ACTION, or called after a TOGGLE changes
    int arg = 0;                  // handed to fn through g_pressArg (the soundboard buttons)
};
static int g_pressArg = 0;
// The owner's layout (2026-10-31). Keep this order.
static Item g_items[] = {
    {MAIN, "Settings", OPEN, SETTINGS},
    {MAIN, "Movement Mods", OPEN, MOVEMENT},
    {MAIN, "Rig Mods", OPEN, RIG},
    {MAIN, "Items", OPEN, ITEMS},
    {MAIN, "Menu Look", OPEN, LOOK},
    {MAIN, "Soundboard", OPEN, SOUNDS},
    {MAIN, "Visual Mods", OPEN, VISUALS},
    {MAIN, "Credits", OPEN, CREDITS},
    {MAIN, "Enabled Mods", OPEN, ENABLED},

    {MOVEMENT, "Exit Movement Mods", OPEN, MAIN},
    {MOVEMENT, "Platforms", TOGGLE, MAIN, &f_platforms},
    {MOVEMENT, "Fly", TOGGLE, MAIN, &f_fly},
    {MOVEMENT, "Fast Fly", TOGGLE, MAIN, &f_fastFly},
    {MOVEMENT, "Velocity Fly", TOGGLE, MAIN, &f_velocityFly},
    {MOVEMENT, "Car", TOGGLE, MAIN, &f_car},
    {MOVEMENT, "Teleport Gun", TOGGLE, MAIN, &f_tpGun},
    {MOVEMENT, "Iron Man", TOGGLE, MAIN, &f_ironMan},
    {MOVEMENT, "Noclip", TOGGLE, MAIN, &f_noclip},
    {MOVEMENT, "Speed Boost", TOGGLE, MAIN, &f_speedBoost, SpeedBoostToggled},
    {MOVEMENT, "Long Arms", TOGGLE, MAIN, &f_longArms},
    {MOVEMENT, "Rope Boost", TOGGLE, MAIN, &f_rope},
    {MOVEMENT, "No Gravity", TOGGLE, MAIN, &f_noGravity, ApplyGravity},
    {MOVEMENT, "Low Gravity", TOGGLE, MAIN, &f_lowGravity},
    {MOVEMENT, "Save Spot", ACTION, MAIN, nullptr, SaveSpot},
    {MOVEMENT, "Go To Spot", ACTION, MAIN, nullptr, GoToSpot},


    {RIG, "Exit Rig Mods", OPEN, MAIN},
    {RIG, "Wave", TOGGLE, MAIN, &g_danceSel[0], DancePicked},
    {RIG, "Helicopter", TOGGLE, MAIN, &g_danceSel[1], DancePicked},
    {RIG, "T-Pose", TOGGLE, MAIN, &g_danceSel[2], DancePicked},
    {RIG, "Windmill", TOGGLE, MAIN, &g_danceSel[3], DancePicked},
    {RIG, "Swim", TOGGLE, MAIN, &g_danceSel[4], DancePicked},
    {RIG, "Jumping Jacks", TOGGLE, MAIN, &g_danceSel[5], DancePicked},
    {RIG, "Spaz", TOGGLE, MAIN, &g_danceSel[6], DancePicked},
    {RIG, "Beyblade", TOGGLE, MAIN, &g_danceSel[7], DancePicked},
    {RIG, "Bird", TOGGLE, MAIN, &g_danceSel[8], DancePicked},
    {RIG, "Clap", TOGGLE, MAIN, &g_danceSel[9], DancePicked},
    {RIG, "Boxing", TOGGLE, MAIN, &g_danceSel[10], DancePicked},

    {ITEMS, "Exit Items", OPEN, MAIN},
    {ITEMS, "Spawn Item Gun", TOGGLE, MAIN, &f_itemGun},
    {ITEMS, "Duplicate Items (3x3)", ACTION, MAIN, nullptr, ArmDupe9},
    {ITEMS, "Duplicate Items (3x3x3)", ACTION, MAIN, nullptr, ArmDupe27},
    {ITEMS, "Spawn On Hand", TOGGLE, MAIN, &f_spawnOnHand},
    {ITEMS, "Item Layouts", OPEN, LAYOUTS},

    {LAYOUTS, "Exit Layouts", OPEN, ITEMS},
    {LAYOUTS, "Floor Mode", TOGGLE, MAIN, &f_layoutFloor},
    {LAYOUTS, "Layout Size", ACTION, MAIN, nullptr, NextLayoutSize},
    {LAYOUTS, "Tree", ACTION, MAIN, nullptr, LayTree},
    {LAYOUTS, "House", ACTION, MAIN, nullptr, LayHouse},
    {LAYOUTS, "Heart", ACTION, MAIN, nullptr, LayHeart},
    {LAYOUTS, "Smiley Face", ACTION, MAIN, nullptr, LaySmiley},
    {LAYOUTS, "Circle", ACTION, MAIN, nullptr, LayCircle},
    {LAYOUTS, "Diamond", ACTION, MAIN, nullptr, LayDiamond},
    {LAYOUTS, "Pyramid", ACTION, MAIN, nullptr, LayPyramid},
    {LAYOUTS, "Plus Sign", ACTION, MAIN, nullptr, LayPlus},
    {LAYOUTS, "Wall", ACTION, MAIN, nullptr, LayWall},
    {LAYOUTS, "Arrow Right", ACTION, MAIN, nullptr, LayRightArrow},
    {LAYOUTS, "Arrow Left", ACTION, MAIN, nullptr, LayLeftArrow},
    {LAYOUTS, "Arrow Up", ACTION, MAIN, nullptr, LayUpArrow},
    {LAYOUTS, "Arrow Down", ACTION, MAIN, nullptr, LayDownArrow},
    {LAYOUTS, "Hi", ACTION, MAIN, nullptr, LayHi},
    {LAYOUTS, "Hello (2 parts)", ACTION, MAIN, nullptr, LayHello},

    {VISUALS, "Exit Visual Mods", OPEN, MAIN},
    {VISUALS, "ESP", TOGGLE, MAIN, &f_esp},
    {VISUALS, "ESP Tracers", TOGGLE, MAIN, &f_espTracers},
    {VISUALS, "No Fog", TOGGLE, MAIN, &f_noFog},
    {VISUALS, "Bright World", TOGGLE, MAIN, &f_bright},

    {CREDITS, "Exit Credits", OPEN, MAIN},
    {CREDITS, "Prisma Client V2", ACTION},
    {CREDITS, "Created by", ACTION},
    {CREDITS, "tmm.ka_yt", ACTION},

    {SETTINGS, "Exit Settings", OPEN, MAIN},
    {SETTINGS, "Change Pointer Position", ACTION, MAIN, nullptr, NextPointer},
    {SETTINGS, "Iron Man Speed", ACTION, MAIN, nullptr, NextIronSpeed},
    {SETTINGS, "Car Speed", ACTION, MAIN, nullptr, NextCarSpeed},
    {SETTINGS, "Boost Strength", ACTION, MAIN, nullptr, NextBoost},
    {SETTINGS, "Arm Length", ACTION, MAIN, nullptr, NextArm},
    {SETTINGS, "Arm Method", ACTION, MAIN, nullptr, NextArmMethod},
    {SETTINGS, "Rope Power", ACTION, MAIN, nullptr, NextRopePower},
    {SETTINGS, "Right Hand Menu", TOGGLE, MAIN, &f_rightHand, PointerToHand},
    {SETTINGS, "Notifications", TOGGLE, MAIN, &f_notify},
    {SETTINGS, "Turn Everything Off", ACTION, MAIN, nullptr, AllOff},

    {LOOK, "Exit Menu Look", OPEN, MAIN},
    {LOOK, "Change Menu Theme", ACTION, MAIN, nullptr, NextTheme},
    {LOOK, "Rainbow Menu", TOGGLE, MAIN, &f_rainbow},
    {LOOK, "Title Style", ACTION, MAIN, nullptr, NextTitleStyle},
    {LOOK, "Menu Size", ACTION, MAIN, nullptr, NextMenuSize},
    {LOOK, "Italic Text", TOGGLE, MAIN, &f_italic, ApplyLook},
    {LOOK, "Menu Outline", TOGGLE, MAIN, &f_outline},
    {LOOK, "Pop Animation", TOGGLE, MAIN, &f_pop},
    {LOOK, "Pointer Size", ACTION, MAIN, nullptr, NextPointerSize},
    {LOOK, "Show FPS", TOGGLE, MAIN, &f_fps},
    {LOOK, "Show Clock", TOGGLE, MAIN, &f_clock},
    {LOOK, "Show Speed", TOGGLE, MAIN, &f_speedo},
};
static constexpr int kItems = sizeof(g_items) / sizeof(g_items[0]);
static Item g_returnItem = {ENABLED, "Exit Enabled Mods", OPEN, MAIN};
static Item g_soundExit = {SOUNDS, "Exit Soundboard", OPEN, MAIN};
static std::vector<Item> g_soundItems;                         // filled by ScanSounds()

// Tooltips, shown under the menu while your finger is on a button (like ii's).
static const struct { const char *label, *tip; } kTips[] = {
    {"Settings", "Opens the settings."},
    {"Movement Mods", "Opens the movement mods."},
    {"Enabled Mods", "Shows every mod you have on."},
    {"Platforms", "Hold a grip to stand on a platform."},
    {"Fly", "Hold A to fly where you look."},
    {"Fast Fly", "Makes Fly a lot faster."},
    {"Velocity Fly", "Hold A to shoot where your hand points."},
    {"Car", "Hold A to drive where you look, floating just above the ground."},
    {"Car Speed", "Slow, normal or fast."},
    {"Teleport Gun", "Hold right grip to aim, trigger to teleport."},
    {"Iron Man", "Hold X and A to fly with your hands."},
    {"Noclip", "Hold right trigger to go through walls."},
    {"Speed Boost", "Your pushes and jumps carry you faster. Strength is in Settings."},
    {"No Gravity", "Turns your gravity off."},
    {"Low Gravity", "Jump higher, fall slower."},
    {"Save Spot", "Remembers where you are."},
    {"Go To Spot", "Teleports you to your saved spot."},
    {"Change Menu Theme", "Prisma, Fire, Orange, Blue, Green, Purple, Sunset, Sakura, Ice, Dark."},
    {"Rainbow Menu", "The menu cycles through colors."},
    {"Show FPS", "Frames per second in the title."},
    {"Show Clock", "The time in the title."},
    {"Rig Mods", "Pick a dance, then hold left trigger. Everyone sees it."},
    {"Items", "Spawn copies of the item in your hand. Everyone sees them."},
    {"Spawn Item Gun", "Hold an item in a hand. Right grip aims, trigger spawns it."},
    {"Duplicate Items (3x3)", "9 of the item in your hand appear in front of you."},
    {"Duplicate Items (3x3x3)", "27 of the item in your hand appear in front of you."},
    {"Spawn On Hand", "Each right trigger spawns the item you are holding at your right hand."},
    {"Wave", "Waves one hand, the other on your hip. Hold left trigger."},
    {"Helicopter", "Both arms spin around you. Hold left trigger."},
    {"T-Pose", "The classic T-pose. Hold left trigger."},
    {"Windmill", "Arms spin like a windmill. Hold left trigger."},
    {"Swim", "Swims through the air. Hold left trigger."},
    {"Jumping Jacks", "Arms go up and down at your sides. Hold left trigger."},
    {"Spaz", "Hands shake all over the place. Hold left trigger."},
    {"Beyblade", "Hands spin fast around your waist. Hold left trigger."},
    {"Change Pointer Position", "Moves the ball on your finger."},
    {"Iron Man Speed", "Changes how strong Iron Man is."},
    {"Right Hand Menu", "Hold B, menu on your right hand."},
    {"Notifications", "Messages when you switch something."},
    {"Menu Look", "Change how the menu looks."},
    {"Soundboard", "Plays .mp3 or .wav files from the mods/sounds folder through your mic."},
    {"Visual Mods", "Things only you see."},
    {"Credits", "Who made this."},
    {"Prisma Client V2", "Just a credit, it does nothing."},
    {"Created by", "Just a credit, it does nothing."},
    {"tmm.ka_yt", "Creator of Prisma Client. Just a credit, it does nothing."},
    {"ESP", "A box around every other player. Only you see it."},
    {"ESP Tracers", "Also a line from your right hand to each player."},
    {"No Fog", "Turns the fog off for you."},
    {"Bright World", "Lights the whole world evenly for you."},
    {"Show Speed", "Your speed in the title."},
    {"Arm Method", "Auto, tracker or simple. Try another if Long Arms does nothing."},
    {"Bird", "Both arms flap like wings. Hold left trigger."},
    {"Clap", "Hands clap in front of you. Hold left trigger."},
    {"Boxing", "Punches, left then right. Hold left trigger."},
    {"Item Layouts", "Pictures made from the item in your hand. Everyone sees them."},
    {"Floor Mode", "Lays the picture flat on the ground instead of standing it up."},
    {"Layout Size", "How far apart the items in a picture are."},
    {"Tree", "A tree made of your held item (18 items)."},
    {"House", "A house made of your held item (24 items)."},
    {"Heart", "A heart made of your held item (25 items)."},
    {"Smiley Face", "A smiley made of your held item (21 items)."},
    {"Circle", "A ring made of your held item (16 items)."},
    {"Diamond", "A diamond made of your held item (13 items)."},
    {"Pyramid", "A pyramid made of your held item (16 items)."},
    {"Plus Sign", "A plus sign made of your held item (9 items)."},
    {"Wall", "A 5 by 5 wall made of your held item (25 items)."},
    {"Arrow Right", "An arrow pointing right (14 items)."},
    {"Arrow Left", "An arrow pointing left (14 items)."},
    {"Arrow Up", "An arrow pointing up (14 items)."},
    {"Arrow Down", "An arrow pointing down (14 items)."},
    {"Hi", "Spells HI (20 items)."},
    {"Hello (2 parts)", "Spells HELLO. Press once for HE, wait, press again for LLO."},
    {"Long Arms", "Your arms reach 20 percent farther (change it in Settings)."},
    {"Rope Boost", "Hold a grip on a rope to swing much farther."},
    {"Boost Strength", "Speed Boost: mild, strong or extreme."},
    {"Arm Length", "How much longer Long Arms makes your arms: 10 to 40 percent."},
    {"Rope Power", "How much extra push you get on ropes: 2x, 3x or 5x."},
    {"Title Style", "Gradient, rainbow, white or solid color letters."},
    {"Menu Size", "Small, normal or large."},
    {"Italic Text", "Slanted or straight letters."},
    {"Menu Outline", "The colored frame around the menu."},
    {"Pop Animation", "The menu pops open when you hold the button."},
    {"Pointer Size", "The size of the ball on your finger."},
    {"Turn Everything Off", "Switches every mod off."},
};
static const char *TipFor(const char *label) {
    if (strncmp(label, "Exit ", 5) == 0) return "Returns you back to the main category.";
    if (g_inSounds) return "Plays through your mic. One at a time.";
    for (auto &t : kTips) if (strcmp(t.label, label) == 0) return t.tip;
    return "";
}

// Colors in the ii menu style: an animated background and darker buttons when on.
struct Theme { const char *name; Color bgA, bgB, button, buttonOn, title; };
static const Theme kThemes[] = {
    {"Prisma", Color(0.10f, 0.80f, 0.95f, 1), Color(0.80f, 0.20f, 0.95f, 1), Color(0.18f, 0.10f, 0.42f, 1), Color(0.55f, 0.30f, 0.95f, 1), Color(0.75f, 0.95f, 1.0f, 1)},
    {"Fire",   Color(0.90f, 0.12f, 0.04f, 1), Color(1.0f, 0.42f, 0.0f, 1), Color(0.50f, 0.06f, 0.03f, 1), Color(1.0f, 0.45f, 0.05f, 1), Color(1.0f, 0.62f, 0.20f, 1)},
    {"Orange", Color(1.0f, 0.50f, 0.0f, 1), Color(1.0f, 0.40f, 0.0f, 1), Color(0.67f, 0.33f, 0.0f, 1), Color(0.33f, 0.16f, 0.0f, 1), Color(1.0f, 0.75f, 0.49f, 1)},
    {"Blue",   Color(0.20f, 0.50f, 1.0f, 1), Color(0.10f, 0.30f, 0.85f, 1), Color(0.10f, 0.28f, 0.65f, 1), Color(0.04f, 0.12f, 0.32f, 1), Color(0.70f, 0.85f, 1.0f, 1)},
    {"Green",  Color(0.30f, 0.85f, 0.30f, 1), Color(0.15f, 0.65f, 0.20f, 1), Color(0.12f, 0.45f, 0.12f, 1), Color(0.05f, 0.22f, 0.05f, 1), Color(0.75f, 1.0f, 0.75f, 1)},
    {"Purple", Color(0.65f, 0.30f, 1.0f, 1), Color(0.45f, 0.15f, 0.85f, 1), Color(0.38f, 0.15f, 0.60f, 1), Color(0.18f, 0.06f, 0.30f, 1), Color(0.88f, 0.75f, 1.0f, 1)},
    {"Sunset", Color(0.98f, 0.38f, 0.38f, 1), Color(0.98f, 0.65f, 0.20f, 1), Color(0.55f, 0.18f, 0.30f, 1), Color(0.25f, 0.08f, 0.15f, 1), Color(1.0f, 0.85f, 0.70f, 1)},
    {"Sakura", Color(1.0f, 0.55f, 0.75f, 1), Color(0.95f, 0.35f, 0.60f, 1), Color(0.65f, 0.25f, 0.42f, 1), Color(0.32f, 0.10f, 0.22f, 1), Color(1.0f, 0.85f, 0.92f, 1)},
    {"Ice",    Color(0.75f, 0.92f, 1.0f, 1), Color(0.55f, 0.80f, 0.98f, 1), Color(0.30f, 0.50f, 0.70f, 1), Color(0.12f, 0.25f, 0.42f, 1), Color(0.95f, 1.0f, 1.0f, 1)},
    {"Dark",   Color(0.12f, 0.12f, 0.14f, 1), Color(0.05f, 0.05f, 0.06f, 1), Color(0.22f, 0.22f, 0.25f, 1), Color(0.45f, 0.45f, 0.50f, 1), Color(0.85f, 0.85f, 0.90f, 1)},
};
static constexpr int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);
static int g_theme = 0;
static int g_titleStyle = 0;                    // 0 gradient, 1 rainbow, 2 white, 3 solid
static const char *kTitleStyleName[] = {"gradient", "rainbow", "white", "solid"};
static const float kMenuSizes[] = {0.8f, 1.0f, 1.25f};
static const char *kMenuSizeName[] = {"small", "normal", "large"};
static int g_menuSize = 1;
static const float kPointerSizes[] = {0.01f, 0.016f, 0.024f};
static int g_pointerSize = 0;
static bool g_sizeDirty = true;
static const Theme &T() { return kThemes[g_theme]; }

// ---- menu objects ----------------------------------------------------------------

// Layout in meters, matching ii's menu as it was ported to Yeeps. Everything is drawn
// on a "board" object: +x goes across, +y goes up, the player looks at it from -z.
static constexpr int kRows = 8;                                   // buttons per page
static const float kBoardW = 0.30f, kBoardH = 0.3825f, kBtnW = 0.27f, kBtnH = 0.0306f;
static const float kTitleY = 0.235f, kSideX = 0.2f, kSideW = 0.06f, kSideH = 0.344f;
static float RowY(int r) { return 0.13f - r * 0.04f; }

static Ref g_menu, g_board, g_bg, g_outline, g_pointer, g_title, g_titleBar;
static Ref g_rowCube[kRows], g_rowLabel[kRows], g_rowText[kRows];
static Ref g_prev, g_next, g_notifyGo, g_notifyText, g_tipText, g_leftPlatform, g_rightPlatform;
static double g_openTime = 0, g_flashUntil = 0;
static int g_flashRow = -2, g_hover = -2;
static bool g_built = false, g_menuShown = false, g_dirty = true;
static Cat g_cat = MAIN;
static int g_page = 0;
static std::vector<Item *> g_view;                                // what the current page list shows
static double g_time = 0, g_nextPress = 0, g_notifyUntil = 0, g_nextTitle = 0;
static int g_touching = -2, g_frames = 0;
static float g_fps = 0;

static int PageCount() { return g_view.empty() ? 1 : (int)(g_view.size() + kRows - 1) / kRows; }

static void BuildView() {
    g_view.clear();
    g_inSounds = g_cat == SOUNDS;
    if (g_cat == ENABLED) {
        g_view.push_back(&g_returnItem);
        for (auto &it : g_items) if (it.kind == TOGGLE && *it.flag && it.flag != &f_notify && it.cat != LOOK) g_view.push_back(&it);
    } else if (g_cat == SOUNDS) {
        g_view.push_back(&g_soundExit);
        for (auto &it : g_soundItems) g_view.push_back(&it);
    } else {
        for (auto &it : g_items) if (it.cat == g_cat) g_view.push_back(&it);
    }
    if (g_page >= PageCount()) g_page = 0;
}

static Il2CppObject *Label(Il2CppObject *canvasT, const char *s, Vector3 pos, float w, float h, int size, Ref *textOut) {
    if (!G.font.get() || !G.tText) return nullptr;
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("Label"));
    Il2CppObject *text = go ? G.goAddComponent[go](G.tText) : nullptr;
    if (!text) return nullptr;
    Il2CppObject *t = Tr(go);
    G.trSetParent[t](canvasT, false);
    G.rtSetSizeDelta[t](Vector2(w * 1000, h * 1000));
    G.trSetLocalPosition[t](pos * 1000);
    G.trSetLocalRotation[t](Quaternion(0, 0, 0, 1));
    G.trSetLocalScale[t](Vector3(1, 1, 1));
    G.textSetFont[text](G.font.get());
    G.textSetText[text](CreateMonoString(s));
    G.textSetFontSize[text](size);
    G.textSetAlignment[text](4);                 // MiddleCenter
    if (G.textSetFontStyle.IsValid()) G.textSetFontStyle[text](f_italic ? 2 : 0);   // Italic, like ii's menu
    if (G.textSetBestFit.IsValid()) {            // shrink long labels to fit their button
        G.textSetBestFit[text](true);
        G.textSetMinSize[text](1);
        G.textSetMaxSize[text](size);
    }
    G.graphicSetColor[text](Color(1, 1, 1, 1));
    if (textOut) textOut->set(text);
    return go;
}

// A world-space canvas (1 unit = 1 mm) under parent.
static Il2CppObject *Canvas(Il2CppObject *parentT, Vector3 pos, float w, float h) {
    Il2CppObject *go = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("PrismaMenuText"));
    Il2CppObject *canvas = go && G.tCanvas ? G.goAddComponent[go](G.tCanvas) : nullptr;
    if (!canvas) return nullptr;
    G.canvasSetRenderMode[canvas](2);            // WorldSpace
    Il2CppObject *ct = Tr(go);
    G.trSetParent[ct](parentT, false);
    G.rtSetSizeDelta[ct](Vector2(w * 1000, h * 1000));
    G.trSetLocalPosition[ct](pos);
    G.trSetLocalRotation[ct](Quaternion(0, 0, 0, 1));
    G.trSetLocalScale[ct](Vector3(0.001f, 0.001f, 0.001f));
    return go;
}

static Il2CppObject *Part(Il2CppObject *parentT, Color c, Vector3 pos, Vector3 scale) {
    Il2CppObject *o = Cube(c, false);
    if (!o) return nullptr;
    Il2CppObject *t = Tr(o);
    G.trSetParent[t](parentT, false);
    G.trSetLocalPosition[t](pos);
    G.trSetLocalScale[t](scale);
    return o;
}

// Where the ball sits on your hand (ii's four pointer positions).
static const Vector3 kPointerSpots[] = {Vector3(0.01f, -0.017f, 0.19f), Vector3(0, -0.1f, 0.05f),
                                        Vector3(0, 0.1f, -0.05f), Vector3(0, 0.0666f, 0.1f)};
static int g_pointerSpot = 0;

static void PointerToHand() {
    Il2CppObject *p = g_pointer.get(), *hand = f_rightHand ? g_leftHand.get() : g_rightHand.get();
    if (!Alive(p) || !hand) return;
    Il2CppObject *t = Tr(p);
    Vector3 at = kPointerSpots[g_pointerSpot];
    if (f_rightHand) at.x = -at.x;
    G.trSetParent[t](hand, false);
    G.trSetLocalPosition[t](at);
}
static void NextPointer() { g_pointerSpot = (g_pointerSpot + 1) % 4; PointerToHand(); Notify("Pointer position %d", g_pointerSpot + 1); }

// Built once per room; afterwards only shown/hidden, moved and relabeled.
static void BuildMenu() {
    auto NewObject = [](const char *name) {
        return Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString(name));
    };
    Il2CppObject *menu = NewObject("PrismaMenu"), *board = NewObject("PrismaMenuBoard");
    if (!menu || !board) return;
    g_menu.set(menu);
    g_board.set(board);
    Il2CppObject *bt = Tr(board);
    G.trSetParent[bt](Tr(menu), false);
    // Like ii's menu: the board stands 10 cm out from the hand on its +x side, facing +x.
    G.trSetLocalPosition[bt](Vector3(0.1f, 0, 0));
    G.trSetLocalRotation[bt](Quaternion::LookRotation(Vector3(-1, 0, 0), Vector3(0, 0, 1)));

    g_outline.set(Part(bt, T().title, Vector3(0, 0, 0.004f), Vector3(kBoardW + 0.008f, kBoardH + 0.008f, 0.004f)));
    g_bg.set(Part(bt, T().bgA, Vector3(0, 0, 0), Vector3(kBoardW, kBoardH, 0.01f)));
    for (int r = 0; r < kRows; r++)
        g_rowCube[r].set(Part(bt, T().button, Vector3(0, RowY(r), -0.005f), Vector3(kBtnW, kBtnH, 0.009f)));
    g_prev.set(Part(bt, T().button, Vector3(-kSideX, 0, 0), Vector3(kSideW, kSideH, 0.009f)));
    // The title sits on its own dark bar above the board (where ii's Disconnect button was)
    g_titleBar.set(Part(bt, T().buttonOn, Vector3(0, kTitleY, -0.002f), Vector3(kBtnW, 0.045f, 0.009f)));
    g_next.set(Part(bt, T().button, Vector3(kSideX, 0, 0), Vector3(kSideW, kSideH, 0.009f)));

    if (Il2CppObject *canvas = Canvas(bt, Vector3(0, 0, -0.0105f), kBoardW + 2 * kSideX, kBoardH + 0.1f)) {
        Il2CppObject *ct = Tr(canvas);
        Label(ct, "Prisma", Vector3(0, kTitleY, 0), kBtnW, 0.045f, 30, &g_title);
        for (int r = 0; r < kRows; r++)
            g_rowLabel[r].set(Label(ct, "", Vector3(0, RowY(r), 0), kBtnW, kBtnH, 19, &g_rowText[r]));
        Label(ct, "<", Vector3(-kSideX, 0, -0.0005f), kSideW, 0.05f, 34, nullptr);
        Label(ct, ">", Vector3(kSideX, 0, -0.0005f), kSideW, 0.05f, 34, nullptr);
        Label(ct, "", Vector3(0, -kBoardH / 2 - 0.02f, 0), kBoardW + 0.1f, 0.03f, 15, &g_tipText);
        char ver[64];
        snprintf(ver, sizeof ver, "Prisma Client V2 | game %s", ym::api()->gameVersion);
        Label(ct, ver, Vector3(0, -kBoardH / 2 + 0.012f, 0), kBoardW, 0.02f, 11, nullptr);
    }
    // Pointer: a small ball on the index finger of the other hand
    if (Il2CppObject *p = Prim(0, T().bgB, false)) {
        G.trSetLocalScale[Tr(p)](Vector3(kPointerSizes[g_pointerSize], kPointerSizes[g_pointerSize], kPointerSizes[g_pointerSize]));
        g_pointer.set(p);
        PointerToHand();
    }
    // Notifications: a line of text low in your view, like ii's notifications
    if (Il2CppObject *n = Canvas(g_head.get(), Vector3(0, -0.12f, 0.6f), 0.5f, 0.05f)) {
        g_notifyGo.set(n);
        Label(Tr(n), "", Vector3(0, 0, 0), 0.5f, 0.05f, 22, &g_notifyText);
        G.goSetActive[n](false);
    }
    G.goSetActive[menu](false);
    Show(g_pointer, false);
    g_built = true;
    g_dirty = true;
}

static void Notify(const char *fmt, ...) {
    if (!f_notify || !Alive(g_notifyGo.get())) return;
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SetText(g_notifyText, buf);
    G.goSetActive[g_notifyGo.get()](true);
    g_notifyUntil = g_time + 2.0;
}

// Relabels and recolors the rows. Only runs after something changed.
static void RenderPage() {
    BuildView();
    for (int r = 0; r < kRows; r++) {
        int i = g_page * kRows + r;
        bool used = i < (int)g_view.size();
        Show(g_rowCube[r], used);
        Show(g_rowLabel[r], used);
        if (!used) continue;
        Item *it = g_view[i];
        bool on = it->kind == TOGGLE && *it->flag;
        SetText(g_rowText[r], it->label);
        SetColor(g_rowCube[r].get(), r == g_flashRow ? Color(1, 0.9f, 0.6f, 1) : on ? T().buttonOn : T().button);
        if (Alive(g_rowText[r].get())) G.graphicSetColor[g_rowText[r].get()](on ? Color(1, 1, 1, 1) : T().title);
    }
    bool pages = PageCount() > 1;
    Show(g_prev, pages);
    Show(g_next, pages);
    SetColor(g_prev.get(), T().button);
    SetColor(g_next.get(), T().button);
    SetColor(g_outline.get(), T().title);
    Show(g_outline, f_outline);
    SetColor(g_titleBar.get(), T().buttonOn);
    SetColor(g_pointer.get(), T().bgB);
    if (Alive(g_title.get())) G.graphicSetColor[g_title.get()](Color(1, 1, 1, 1));
    g_nextTitle = 0;                     // refresh the title now
    g_dirty = false;
}

static int Hex(float v) { return (int)(v < 0 ? 0 : v > 1 ? 255 : v * 255); }

// The title is "Prisma Client V2" with a moving gradient, one color per letter
// (rich text). Rebuilt ten times a second, not every frame.
static void UpdateTitle() {
    if (g_time < g_nextTitle) return;
    g_nextTitle = g_time + 0.1;
    const Theme &th = T();
    const Color pal[4] = {th.title, th.bgB, th.bgA, th.bgB};
    char buf[512];
    int n = 0;
    const char *word = "Prisma";
    for (int i = 0; word[i]; i++) {
        float t = (float)g_time * 1.5f - i * 0.35f;
        Color c;
        if (f_rainbow || g_titleStyle == 1) c = Rainbow(t * 0.2f);
        else if (g_titleStyle == 2) c = Color(1, 1, 1, 1);
        else if (g_titleStyle == 3) c = th.title;
        else {
            float f = t - floorf(t / 4) * 4;          // 0..4 along the theme colors
            int k = (int)f;
            c = Lerp(pal[k], pal[(k + 1) % 4], f - k);
        }
        n += snprintf(buf + n, sizeof buf - n, "<b><color=#%02X%02X%02X>%c</color></b>", Hex(c.r), Hex(c.g), Hex(c.b), word[i]);
    }
    n += snprintf(buf + n, sizeof buf - n, "<size=20><color=#%02X%02X%02X><b> Client V2</b></color></size>", Hex(th.title.r), Hex(th.title.g), Hex(th.title.b));
    n += snprintf(buf + n, sizeof buf - n, " <color=grey>[</color><color=white>%d</color><color=grey>]</color>", g_page + 1);
    if (f_fps) n += snprintf(buf + n, sizeof buf - n, "<size=16>  %d fps</size>", (int)(g_fps + 0.5f));
    if (f_speedo) n += snprintf(buf + n, sizeof buf - n, "<size=16>  %.1f m/s</size>", g_speedNow);
    if (f_clock) {
        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        n += snprintf(buf + n, sizeof buf - n, "<size=16>  %d:%02d</size>", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
    }
    SetText(g_title, buf);
}

static void ShowMenu(bool show) {
    if (show == g_menuShown) return;
    g_menuShown = show;
    if (show) { g_openTime = g_time; g_dirty = true; g_hover = -2; g_sizeDirty = true; SetText(g_tipText, ""); }
    Show(g_menu, show);
    Show(g_pointer, show);
    g_touching = -2;
}

static float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Like ii's menu: the menu follows your hand and is tipped 45 degrees, so you read it
// by turning your palm toward your face. "Floating Menu" instead keeps the board above
// your hand, always facing you.
static void PlaceMenu() {
    Il2CppObject *menu = g_menu.get(), *board = g_board.get(), *head = g_head.get();
    Il2CppObject *hand = f_rightHand ? g_rightHand.get() : g_leftHand.get();
    if (!Alive(menu) || !Alive(board) || !hand || !head) return;
    Il2CppObject *t = Tr(menu), *bt = Tr(board);
    Vector3 handPos = G.trGetPosition[hand]();
    if (!G.trGetRotation.IsValid()) {                  // fallback: float above the hand, facing you
        Vector3 center = handPos + Vector3(0, 0.25f, 0), away = center - G.trGetPosition[head]();
        if (away.x * away.x + away.y * away.y + away.z * away.z < 1e-6f) return;
        G.trSetPosition[t](handPos);
        G.trSetPosition[bt](center);
        G.trSetRotation[bt](Quaternion::LookRotation(away));
        return;
    }
    const float s = 0.38268343f, c = 0.92387953f;                  // sin/cos of 22.5 degrees
    Quaternion rot = G.trGetRotation[hand]() * Quaternion(-s, 0, 0, c);   // tip -45 degrees on x
    if (f_rightHand) rot = rot * Quaternion(0, 0, 1, 0);           // mirrored: 180 degrees on z
    G.trSetPosition[t](handPos);
    G.trSetRotation[t](rot);
    G.trSetLocalPosition[bt](Vector3(0.1f, 0, 0));
    G.trSetLocalRotation[bt](Quaternion::LookRotation(Vector3(-1, 0, 0), Vector3(0, 0, 1)));
}

static void Press(Item *it) {
    switch (it->kind) {
    case OPEN:
        g_cat = it->target;
        g_page = 0;
        if (it->target == SOUNDS) {
            ScanSounds();
            if (g_soundItems.empty()) Notify("Put .mp3 or .wav files in the mods/sounds folder");
        }
        break;
    case TOGGLE:
        *it->flag = !*it->flag;
        if (it->fn) it->fn();
        Notify("[%s] %s", *it->flag ? "ON" : "OFF", it->label);
        break;
    case ACTION:
        g_pressArg = it->arg;
        if (it->fn) it->fn();
        break;
    }
    g_dirty = true;
    ym::log("prisma: %s", it->label);
}

// Which part of the menu the pointer touches: row 0..7, -1 = <, kRows = >, -2 = nothing.
static int Touching() {
    Il2CppObject *board = g_board.get(), *pointer = g_pointer.get();
    if (!Alive(board) || !Alive(pointer)) return -2;
    Vector3 p = G.trInverseTransformPoint[Tr(board)](G.trGetPosition[Tr(pointer)]());
    if (p.z < -0.035f || p.z > 0.015f) return -2;
    if (fabsf(fabsf(p.x) - kSideX) < kSideW / 2 && fabsf(p.y) < kSideH / 2 && PageCount() > 1)
        return p.x < 0 ? -1 : kRows;
    if (fabsf(p.x) >= kBtnW / 2) return -2;
    for (int r = 0; r < kRows; r++)
        if (fabsf(p.y - RowY(r)) < kBtnH / 2) return r;
    return -2;
}

static void Buzz() {
    if (!G.haptic.IsValid()) return;
    XRDevice d = G.deviceAtNode(f_rightHand ? 4 : 5);    // the hand that presses
    if (d.initialized) G.haptic(d.id, 0u, 0.35f, 0.04f);
}

static void CheckPress() {
    int hit = Touching();
    if (hit != g_hover) {                                 // tooltip for the button under your finger
        g_hover = hit;
        int i = g_page * kRows + hit;
        if (hit >= 0 && hit < kRows && i < (int)g_view.size()) SetText(g_tipText, TipFor(g_view[i]->label));
    }
    if (hit != -2 && hit != g_touching && g_time >= g_nextPress) {   // one press per touch
        g_nextPress = g_time + 0.25;
        Buzz();
        if (hit >= 0 && hit < kRows) { g_flashRow = hit; g_flashUntil = g_time + 0.12; }
        if (hit == -1 || hit == kRows) {
            int n = PageCount();
            g_page = (g_page + (hit == kRows ? 1 : n - 1)) % n;
            g_dirty = true;
        } else {
            int i = g_page * kRows + hit;
            if (i < (int)g_view.size()) Press(g_view[i]);
        }
    }
    g_touching = hit;
}

static void AnimateMenu() {
    float open = f_pop ? (float)(g_time - g_openTime) / 0.15f : 1.0f;     // pop open like ii's menu
    if (((f_pop && open <= 1.05f) || g_sizeDirty) && Alive(g_menu.get())) {
        float k = open >= 1 ? 1 : 1 - (1 - open) * (1 - open);
        k *= kMenuSizes[g_menuSize];
        G.trSetLocalScale[Tr(g_menu.get())](Vector3(k, k, k));
        g_sizeDirty = false;
    }
    if (g_flashRow != -2 && g_time >= g_flashUntil) { g_flashRow = -2; g_dirty = true; }
    float t = 0.5f + 0.5f * sinf((float)g_time * 2.0f);
    Color c = f_rainbow ? Rainbow((float)g_time * 0.15f) : Lerp(T().bgA, T().bgB, t);
    SetColor(g_bg.get(), c);
}

// ---- features (local only) -----------------------------------------------------

static Il2CppObject *PlayerTransform() {
    Il2CppObject *player = g_player.get();
    return player ? G.compTransform[player]() : nullptr;
}

// Your body's velocity, through the game's own player functions (Rigidbody as backup).
static bool GetVel(Vector3 &v) {
    Il2CppObject *player = g_player.get(), *rb = g_rigidbody.get();
    if (player && G.plGetVelocity.IsValid()) { v = G.plGetVelocity[player](); return true; }
    if (rb && G.rbGetVelocity.IsValid()) { v = G.rbGetVelocity[rb](); return true; }
    return false;
}
static void SetVel(Vector3 v) {
    Il2CppObject *player = g_player.get(), *rb = g_rigidbody.get();
    if (player && G.plSetVelocity.IsValid()) G.plSetVelocity[player](v);
    else if (rb && G.rbSetVelocity.IsValid()) G.rbSetVelocity[rb](v);
}

// Where your body is (the body collider), which is what teleports aim for.
static bool BodyPos(Vector3 &out) {
    Il2CppObject *player = g_player.get(), *t = PlayerTransform();
    if (!player || !t) return false;
    Il2CppObject *body = G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr;
    out = body ? G.trGetPosition[G.compTransform[body]()]() : G.trGetPosition[t]();
    return true;
}
static void StopMoving() { SetVel(Vector3(0, 0, 0)); }
// Moves your body to pos, using the game's own TeleportTo when it has one.
static void TeleportPlayer(Vector3 pos) {
    Il2CppObject *player = g_player.get(), *t = PlayerTransform();
    Vector3 body;
    if (!player || !t || !BodyPos(body)) return;
    Vector3 target = pos - body + G.trGetPosition[t]();
    if (G.teleportTo.IsValid()) G.teleportTo[player](target);
    else G.trSetPosition[t](target);
    StopMoving();
}

static void SaveSpot() {
    if (BodyPos(g_spot)) { g_haveSpot = true; Notify("Spot saved"); }
}
static void GoToSpot() {
    if (!g_haveSpot) { Notify("Save a spot first"); return; }
    TeleportPlayer(g_spot);
    Notify("Teleported to your spot");
}

static const float kIronSpeeds[] = {15, 12, 8, 4};
static int g_ironSpeed = 0;
static void NextIronSpeed() { g_ironSpeed = (g_ironSpeed + 1) % 4; Notify("Iron Man speed: %d", (int)kIronSpeeds[g_ironSpeed]); }
static void NextTheme() { g_theme = (g_theme + 1) % kThemeCount; Notify("Theme: %s", T().name); }
static void AllOff() {
    for (auto &it : g_items) if (it.kind == TOGGLE && it.flag != &f_notify && it.cat != LOOK) *it.flag = false;   // ticks undo each mod
    g_dance = -1;
    g_dupeArmed = 0;
    g_itemQueue.clear();
    PointerToHand();
    Notify("Everything off");
}

static void FlyTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *t = PlayerTransform();
    if (!f_fly || !in.a || !head || !t) return;
    Vector3 pos = G.trGetPosition[t]() + G.trGetForward[head]() * ((f_fastFly ? 20.0f : 8.0f) * dt);
    G.trSetPosition[t](pos);
    StopMoving();
}

static void LowGravityTick(const Buttons &in, float dt) {
    Vector3 v;
    if (!f_lowGravity || f_noGravity || (f_fly && in.a) || !GetVel(v)) return;
    SetVel(v + Vector3(0, 9.81f * 0.7f * dt, 0));       // cancels 70% of gravity
}

static void PlatformFor(bool held, Ref &platform, Ref &hand) {
    if (!held || !f_platforms) {
        Kill(platform);
        return;
    }
    if (platform.get()) return;                             // already standing on one
    if (!hand.get()) return;
    Il2CppObject *p = Cube(T().button, true);               // keeps its collider: you stand on it
    if (!p) return;
    // Hands only collide with the layers in the player's locomotionEnabledLayers (like
    // Gorilla Tag), so the platform goes on the first of those layers.
    static int layer = -2;
    if (layer == -2) {
        layer = -1;
        int mask = G.locomotionLayers.IsValid() && g_player.get() ? G.locomotionLayers[g_player.get()]() : 0;
        for (int i = 0; i < 32 && layer < 0; i++) if (mask & (1 << i)) layer = i;
        ym::log("prisma: hand layers 0x%x, platforms on layer %d", mask, layer);
    }
    if (layer >= 0 && G.goSetLayer.IsValid()) G.goSetLayer[p](layer);
    Il2CppObject *t = Tr(p);
    G.trSetPosition[t](G.trGetPosition[hand.get()]() - Vector3(0, 0.08f, 0));
    G.trSetLocalScale[t](Vector3(0.28f, 0.02f, 0.28f));
    platform.set(p);
}

static void VelocityFlyTick(const Buttons &in) {
    Il2CppObject *hand = g_rightHand.get();
    if (!f_velocityFly || !in.a || !hand) return;
    SetVel(G.trGetForward[hand]() * 20.0f);
}

static Vector3 Flat(Vector3 v);

// Car: like Fly, but along the ground. Holding A drives you the way you look, flat, and
// you hover a little above the ground (a raycast down finds it) so the bumps on blocks
// don't catch you. Off a ledge, with no ground close below, you fall normally.
static const float kCarSpeeds[] = {4.0f, 6.0f, 9.0f};
static const char *kCarNames[] = {"slow", "normal", "fast"};
static int g_carSpeed = 1;
static float g_carHeight = -1;               // your head's height above the ground, measured once
static void NextCarSpeed() { g_carSpeed = (g_carSpeed + 1) % 3; Notify("Car: %s", kCarNames[g_carSpeed]); }
static void CarTick(const Buttons &in) {
    Il2CppObject *head = g_head.get(), *player = g_player.get();
    Vector3 v;
    if (!f_car) { g_carHeight = -1; return; }
    if (!in.a || !head || !player || !GetVel(v) || !G.raycast.IsValid()) return;
    int mask = G.locomotionLayers.IsValid() ? G.locomotionLayers[player]() : -5;
    Vector3 hp = G.trGetPosition[head]();
    BNM::Structures::Unity::RaycastHit hit{};
    bool ground = G.raycast(hp, Vector3(0, -1, 0), &hit, 3.0f, mask ? mask : -5);
    Vector3 f = Flat(G.trGetForward[head]()) * kCarSpeeds[g_carSpeed];
    float vy = fminf(v.y, 1.0f);
    if (ground) {
        if (g_carHeight < 0) g_carHeight = hp.y - hit.point.y;      // your standing height
        float target = hit.point.y + g_carHeight + 0.15f;            // float 15 cm higher
        vy = (target - hp.y) * 8.0f;
        if (G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.1f);
    }
    SetVel(Vector3(f.x, vy, f.z));
}

static void IronManTick(const Buttons &in, float dt) {
    Vector3 v;
    if (!f_ironMan || !G.trGetRight.IsValid() || (!in.x && !in.a) || !GetVel(v)) return;
    float speed = kIronSpeeds[g_ironSpeed] * dt;
    if (in.x && g_leftHand.get()) v = v + G.trGetRight[g_leftHand.get()]() * -speed;
    if (in.a && g_rightHand.get()) v = v + G.trGetRight[g_rightHand.get()]() * speed;
    SetVel(v);
}

// Speed Boost and No Gravity change your own player; turning them off puts the old
// values back. Checked every frame so they also come back after a room change.
static bool g_gravityOff = false;

static void ApplyGravity() {
    // The game's own "no gravity for a while" (what its levitation items use), renewed
    // every frame while the mod is on; it wears off by itself once you turn it off.
    Il2CppObject *player = g_player.get();
    if (f_noGravity && player && G.plNoGravityFor.IsValid()) G.plNoGravityFor[player](0.25f);
    g_gravityOff = f_noGravity;
}

// Noclip: while you hold the right trigger you pass through everything. Yeeps moves
// your body with its own layer lists (locomotionEnabledLayers for your hands,
// collisionEnabledLayers for your body), so those are emptied, physics ignores your
// body against those layers, and your own colliders are off. Gravity stays normal, like
// Gorilla Tag noclip. All of it goes back when you let go. Only your own player changes; nothing is sent.
static bool g_noclipOn = false;
static Ref g_noclipOff[24];                    // your own colliders switched off during noclip
static int g_noclipOffN = 0;
// Every solid collider on your own player (body, head, hands...), switched off while
// noclip is held so nothing of you hits the level.
static void PlayerColliders(bool off) {
    if (!off) {
        for (int i = 0; i < g_noclipOffN; i++) {
            if (Alive(g_noclipOff[i].get())) G.colliderSetEnabled[g_noclipOff[i].get()](true);
            g_noclipOff[i].clear();
        }
        g_noclipOffN = 0;
        return;
    }
    Il2CppObject *player = g_player.get();
    if (!player || !G.colliderSetEnabled.IsValid()) return;
    auto offOne = [](Il2CppObject *c) {
        if (!Alive(c) || g_noclipOffN >= 24 || !G.behaviourGetEnabledCollider[c]()) return;
        if (G.colliderIsTrigger.IsValid() && G.colliderIsTrigger[c]()) return;
        for (int i = 0; i < g_noclipOffN; i++) if (g_noclipOff[i].get() == c) return;
        G.colliderSetEnabled[c](false);
        g_noclipOff[g_noclipOffN++].set(c);
    };
    // Your body and head colliders (fields on the player), then every solid collider in
    // the rig they belong to.
    Il2CppObject *own[2] = {G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr,
                            G.headCollider.IsValid() ? G.headCollider[player]() : nullptr};
    for (Il2CppObject *c : own) {
        if (!c) continue;
        offOne(c);
        Il2CppObject *rig = G.compGameObject[G.trGetRoot[G.compTransform[c]()]()]();
        auto *arr = rig && G.getComponentsInChildren.IsValid()
                        ? (uint8_t *)G.getComponentsInChildren[rig]((Il2CppObject *)G.tCollider, false) : nullptr;
        size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
        for (size_t i = 0; i < n; i++) offOne(((Il2CppObject **)(arr + 0x20))[i]);
    }
}
static int g_savedLoco = 0, g_savedColl = 0, g_bodyLayer = -1;
static void NoclipTick(const Buttons &in) {
    Il2CppObject *player = g_player.get();
    bool want = f_noclip && in.rtrig && player;
    if (want == g_noclipOn) return;
    g_noclipOn = want;
    if (!player || !G.locomotionLayers.IsValid()) return;
    if (want) {
        g_savedLoco = G.locomotionLayers[player]();
        g_savedColl = G.collisionLayers.IsValid() ? G.collisionLayers[player]() : 0;
        Il2CppObject *body = G.bodyCollider.IsValid() ? G.bodyCollider[player]() : nullptr;
        Il2CppObject *bodyGo = body ? G.compGameObject[body]() : nullptr;
        g_bodyLayer = bodyGo && G.goGetLayer.IsValid() ? G.goGetLayer[bodyGo]() : -1;
    }
    int mask = g_savedLoco | g_savedColl;
    G.locomotionLayers[player] = want ? 0 : g_savedLoco;
    if (G.collisionLayers.IsValid()) G.collisionLayers[player] = want ? 0 : g_savedColl;
    if (g_bodyLayer >= 0 && G.ignoreLayers.IsValid())
        for (int l = 0; l < 32; l++)
            if (mask & (1 << l)) G.ignoreLayers(g_bodyLayer, l, want);
    PlayerColliders(want);
    if (want) ym::log("prisma: noclip on (layers 0x%x, body layer %d, %d colliders off)", mask, g_bodyLayer, g_noclipOffN);
}

// Teleport Gun: hold right grip to aim (a line and a ball show where you land), press
// the right trigger to go there.
static Ref g_gunLine, g_gunLineGo, g_gunBall;
static bool g_gunTrigger = false;
static void TeleportGunTick(const Buttons &in) {
    bool aiming = (f_tpGun || f_itemGun) && in.rgrip;
    if (!aiming) {
        Show(g_gunLineGo, false);
        Show(g_gunBall, false);
        g_gunTrigger = in.rtrig;
        return;
    }
    Il2CppObject *hand = g_rightHand.get();
    if (!hand || !G.raycast.IsValid() || !G.lrSetPosition.IsValid()) return;
    if (!g_gunBall.get()) {                              // made once per room
        Il2CppObject *ball = Prim(0, T().bgB, false);
        Il2CppObject *lineGo = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("PrismaGunLine"));
        Il2CppObject *line = lineGo && G.tLine ? G.goAddComponent[lineGo](G.tLine) : nullptr;
        if (!ball || !line) return;
        G.trSetLocalScale[Tr(ball)](Vector3(0.1f, 0.1f, 0.1f));
        if (Il2CppObject *m = G.rendererGetMaterial[line]()) {
            if (G.shader.get()) G.matSetShader[m](G.shader.get());
            G.matSetColor[m](T().bgB);
        }
        G.lrSetStartWidth[line](0.025f);
        G.lrSetEndWidth[line](0.025f);
        G.lrSetPositionCount[line](2);
        G.lrSetUseWorldSpace[line](true);
        g_gunBall.set(ball);
        g_gunLine.set(line);
        g_gunLineGo.set(lineGo);
    }
    Vector3 start = G.trGetPosition[hand](), dir = G.trGetForward[hand]();
    BNM::Structures::Unity::RaycastHit hit{};
    bool found = G.raycast(start + dir * 0.25f, dir, &hit, 512.0f, -5);   // Physics.DefaultRaycastLayers
    Vector3 end = found ? hit.point : start + dir * 512.0f;
    Show(g_gunLineGo, true);
    Show(g_gunBall, true);
    G.trSetPosition[Tr(g_gunBall.get())](end);
    G.lrSetPosition[g_gunLine.get()](0, start);
    G.lrSetPosition[g_gunLine.get()](1, end);
    SetColor(g_gunBall.get(), in.rtrig ? T().buttonOn : T().bgB);
    if (in.rtrig && !g_gunTrigger && found) {
        if (f_itemGun) {                                  // Spawn Item Gun: an item at that spot
            if (g_time >= g_nextItem && HeldItem()) {
                g_nextItem = g_time + 0.5;
                g_itemQueue.push_back({end + Vector3(0, 0.3f, 0), Vector3(0, 0, 0)});
            }
        } else TeleportPlayer(end + Vector3(0, 1.0f, 0));
    }
    g_gunTrigger = in.rtrig;
}

// ---- vector helpers ----------------------------------------------------------------

static Vector3 Cross(Vector3 a, Vector3 b) { return Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static Vector3 Flat(Vector3 v) {                      // v without its up/down part, length 1
    v.y = 0;
    float l = sqrtf(v.x * v.x + v.z * v.z);
    return l > 1e-4f ? v * (1.0f / l) : Vector3(0, 0, 1);
}

// ---- rig mods: dances -----------------------------------------------------------
// While you hold the left trigger, your hands are moved through a dance instead of
// following your controllers. The game sends your hand positions to the room as
// usual, so other players see you dance. Nothing extra is sent.

// The dance buttons work like radio buttons: picking one turns the others off,
// pressing the picked one again turns dancing off.
static void DancePicked() {
    int picked = -1;
    for (int i = 0; i < kDances; i++) if (g_danceSel[i] && i != g_dance) picked = i;
    for (int i = 0; i < kDances; i++) g_danceSel[i] = (i == picked);
    g_dance = picked;
    if (picked >= 0) Notify("Hold left trigger to dance");
}
static float Rand01() { static uint32_t x = 2463534242u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (x & 0xffff) / 65535.0f; }

// The controller trackers would put your hands back every frame, so they are paused
// while you dance (on the hand and the two objects above it) and turned back on after.
static Ref g_paused[12];
static int g_pausedN = 0;
static bool g_dancing = false;
static void PauseTrackers(bool pause) {
    if (!pause) {
        for (int i = 0; i < g_pausedN; i++) {
            if (Alive(g_paused[i].get())) G.behaviourSetEnabled[g_paused[i].get()](true);
            g_paused[i].clear();
        }
        g_pausedN = 0;
        return;
    }
    if (!G.behaviourSetEnabled.IsValid() || !G.trGetParent.IsValid()) return;
    for (Il2CppObject *t : {g_leftHand.get(), g_rightHand.get()})
        for (int up = 0; up < 3 && t; up++, t = G.trGetParent[t]())
            for (BNM::MonoType *type : G.trackerTypes) {
                Il2CppObject *go = G.compGameObject[t]();
                Il2CppObject *c = go ? G.goGetComponent[go]((Il2CppObject *)type) : nullptr;
                if (!c || !G.behaviourGetEnabled[c]() || g_pausedN >= 12) continue;
                G.behaviourSetEnabled[c](false);
                g_paused[g_pausedN++].set(c);
            }
}

static void DanceTick(const Buttons &in, float dt) {
    Il2CppObject *head = g_head.get(), *lh = g_leftHand.get(), *rh = g_rightHand.get();
    bool want = g_dance >= 0 && in.ltrig && head && lh && rh;
    if (want != g_dancing) {
        PauseTrackers(want);
        g_dancing = want;
        if (want) ym::log("prisma: dancing, paused %d trackers", g_pausedN);
    }
    if (!want) return;
    Vector3 hp = G.trGetPosition[head](), f = Flat(G.trGetForward[head]()), up(0, 1, 0);
    Vector3 r = Cross(up, f);                            // your right
    Vector3 shoulder = hp + up * -0.25f;
    float t = (float)g_time, s = sinf(t * 6), c = cosf(t * 6);
    Vector3 L, R;                                        // hand positions
    switch (g_dance) {
    case 0:   // Wave: right hand up and waving, left hand on the hip
        R = shoulder + r * (0.35f + 0.12f * s) + up * 0.45f + f * 0.1f;
        L = shoulder + r * -0.25f + up * -0.25f + f * 0.05f;
        break;
    case 1:   // Helicopter: both hands spin around you
        R = shoulder + r * (0.6f * cosf(t * 8)) + f * (0.6f * sinf(t * 8)) + up * 0.15f;
        L = shoulder - r * (0.6f * cosf(t * 8)) - f * (0.6f * sinf(t * 8)) + up * 0.15f;
        break;
    case 2:   // T-Pose
        R = shoulder + r * 0.7f + up * 0.1f;
        L = shoulder - r * 0.7f + up * 0.1f;
        break;
    case 3:   // Windmill: big circles at your sides
        R = shoulder + r * 0.4f + up * (0.5f * sinf(-t * 5)) + f * (0.5f * cosf(-t * 5));
        L = shoulder - r * 0.4f - up * (0.5f * sinf(-t * 5)) - f * (0.5f * cosf(-t * 5));
        break;
    case 4: {  // Swim: breaststroke
        float p = t * 3;
        R = shoulder + f * (0.3f + 0.25f * cosf(p)) + r * (0.08f + 0.35f * fabsf(sinf(p)));
        L = shoulder + f * (0.3f + 0.25f * cosf(p)) - r * (0.08f + 0.35f * fabsf(sinf(p)));
        break;
    }
    case 5: {  // Jumping jacks: arms from down by your sides to up over your head
        float a = (sinf(t * 6) * 0.5f + 0.5f) * 3.1f - 1.55f;
        R = shoulder + r * (0.6f * cosf(a)) + up * (0.6f * sinf(a));
        L = shoulder - r * (0.6f * cosf(a)) + up * (0.6f * sinf(a));
        break;
    }
    case 6:   // Spaz: hands in front, shaking everywhere
        R = shoulder + f * 0.35f + r * 0.2f + Vector3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f) * 0.3f;
        L = shoulder + f * 0.35f - r * 0.2f + Vector3(Rand01() - 0.5f, Rand01() - 0.5f, Rand01() - 0.5f) * 0.3f;
        break;
    case 8:   // Bird: both arms flap like wings
        R = shoulder + r * 0.7f + up * (0.1f + 0.35f * sinf(t * 9));
        L = shoulder - r * 0.7f + up * (0.1f + 0.35f * sinf(t * 9));
        break;
    case 9:   // Clap: hands meet in front of you
        R = shoulder + f * 0.4f + r * (0.02f + 0.2f * fabsf(sinf(t * 7))) - up * 0.05f;
        L = shoulder + f * 0.4f - r * (0.02f + 0.2f * fabsf(sinf(t * 7))) - up * 0.05f;
        break;
    case 10:  // Boxing: left, then right
        R = shoulder + f * (0.25f + 0.4f * fmaxf(0.0f, sinf(t * 8))) + r * 0.15f;
        L = shoulder + f * (0.25f + 0.4f * fmaxf(0.0f, -sinf(t * 8))) - r * 0.15f;
        break;
    default:  // Beyblade: hands spin fast around your waist
        R = shoulder - up * 0.3f + r * (0.5f * cosf(t * 20)) + f * (0.5f * sinf(t * 20));
        L = shoulder - up * 0.3f - r * (0.5f * cosf(t * 20)) - f * (0.5f * sinf(t * 20));
        break;
    }
    // Keep each hand within arm's reach of its shoulder, then glide there instead of
    // jumping, so the arms move smoothly for you and for everyone watching.
    auto reach = [&](Vector3 hand, Vector3 sh) {
        Vector3 d = hand - sh;
        float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        return len > 0.7f ? sh + d * (0.7f / len) : hand;
    };
    R = reach(R, shoulder + r * 0.18f);
    L = reach(L, shoulder - r * 0.18f);
    float k = g_dance == 6 ? 1.0f : fminf(1.0f, dt * 14.0f);     // Spaz stays twitchy
    G.trSetPosition[lh](G.trGetPosition[lh]() + (L - G.trGetPosition[lh]()) * k);
    G.trSetPosition[rh](G.trGetPosition[rh]() + (R - G.trGetPosition[rh]()) * k);
    Quaternion look = Quaternion::LookRotation(f, up);
    G.trSetRotation[lh](look);
    G.trSetRotation[rh](look);
}

// ---- items (everyone sees these) ------------------------------------------------
// Items are made with the game's own item message (RpcCreateItem on the room's
// NetworkMessenger), the same one the game sends when an item appears. Owner-approved
// 2026-10-31. Every way of spawning has a cooldown, and big batches are sent a few per
// frame, so the room can't be flooded.


static void Put(Il2CppObject *arr, size_t i, void *v) { ((void **)((uint8_t *)arr + 0x20))[i] = v; }

// Builds RpcCreateItem's arguments from its own parameter list, in the order the game
// uses: item id, item key, spawn kind 1, no owner, position, rotation, velocity, spin,
// properties, 1, 1.
static bool SendItem(const char *key, Vector3 pos, Quaternion rot, Vector3 vel, char *idOut = nullptr) {
    auto *mi = G.rpcCreateItem;
    Il2CppObject *nm = G.netLocal.IsValid() ? G.netLocal() : nullptr;
    if (!mi || !nm || !G.pvRpc.IsValid() || !G.arrayNew || !G.valueBox || !G.tPhotonView) return false;
    Il2CppObject *go = G.compGameObject[nm]();
    Il2CppObject *pv = go ? G.goGetComponent[go]((Il2CppObject *)G.tPhotonView) : nullptr;
    if (!pv) return false;
    // The last parameter (PhotonMessageInfo) is filled in by Photon on the receiving
    // side; it is never sent.
    int count = mi->parameters_count;
    if (count > 0 && TypeName(mi->parameters[count - 1]) == "Photon.Pun.PhotonMessageInfo") count--;
    auto *args = (Il2CppObject *)G.arrayNew(G.objectClass, count);
    if (!args) return false;
    char id[8];
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < 4; i++) id[i] = kChars[(int)(Rand01() * 35.99f)];
    id[4] = 0;
    if (idOut) memcpy(idOut, id, 5);
    int strings = 0, vectors = 0, others = 0;
    for (int i = 0; i < count; i++) {
        std::string tn = TypeName(mi->parameters[i]);
        Class pc(mi->parameters[i]);
        auto *k = pc.IsValid() ? pc.GetClass() : nullptr;
        void *v = nullptr;
        if (tn == "System.String") {
            if (strings == 0) v = CreateMonoString(id);
            else if (strings == 1) v = CreateMonoString(key);
            strings++;                                   // later strings stay null
        } else if (tn == "UnityEngine.Vector3") {
            Vector3 vec = vectors == 0 ? pos : vectors == 1 ? vel : Vector3(0, 0, 0);
            vectors++;
            v = G.valueBox(k, &vec);
        } else if (tn == "UnityEngine.Quaternion") {
            v = G.valueBox(k, &rot);
        } else if (tn == "System.String[]") {
            v = G.arrayNew(G.stringClass, 0);
        } else if (k && k->byval_arg.valuetype) {
            uint8_t raw[64] = {};
            raw[0] = 1;                                 // spawn kind 1 / the two bytes = 1
            // Photon can't send the game's enum types, only plain numbers (like the kit's
            // Int32 1), so an enum goes as its underlying number type.
            auto *boxAs = k->enumtype && k->element_class ? k->element_class : k;
            v = G.valueBox(boxAs, raw);
            others++;
        }
        Put(args, i, v);
    }
    static bool loggedTypes = false;
    if (!loggedTypes) {                                   // once: what the game expects, for fixing
        loggedTypes = true;
        std::string sig;
        for (int i = 0; i < mi->parameters_count; i++) {
            Class pc(mi->parameters[i]);
            auto *k = pc.IsValid() ? pc.GetClass() : nullptr;
            std::string tn = TypeName(mi->parameters[i]);
            if (tn.size() > 30) tn = k && k->byval_arg.valuetype ? (k->enumtype ? "enum" : "struct" + std::to_string(k->instance_size - 16)) : "class";
            sig += (i ? "," : "") + tn;
        }
        ym::log("prisma: item message takes %s", sig.c_str());
    }
    try {
        G.pvRpc[pv](CreateMonoString("RpcCreateItem"), 0, args);      // 0 = everyone in the room
    } catch (BNM::IL2CPP::Il2CppExceptionWrapper &w) {   // the game said no: log why, don't crash the menu
        auto *ex = w.ex;
        auto *msg = ex ? (BNM::Structures::Mono::String *)ex->message : nullptr;
        auto *cls = ex ? (BNM::Structures::Mono::String *)ex->className : nullptr;
        ym::log("prisma: item message error %s: %s", cls ? cls->str().c_str() : "?", msg ? msg->str().c_str() : "?");
        return false;
    }
    return true;
}

static bool QueueItems(const std::vector<PendingItem> &list) {
    if (!g_itemKey[0]) return false;
    if (g_time < g_nextBatch) { Notify("Wait a moment"); return false; }
    g_nextBatch = g_time + 3.0;                       // one batch every 3 seconds at most
    int added = 0;
    for (auto &p : list) if (added < 27 && g_itemQueue.size() < 32) { g_itemQueue.push_back(p); added++; }
    return true;
}

static void ItemQueueTick() {
    for (int sent = 0; sent < 2 && !g_itemQueue.empty(); sent++) {   // 2 a frame at most
        PendingItem p = g_itemQueue.front();
        g_itemQueue.erase(g_itemQueue.begin());
        bool ok = SendItem(g_itemKey, p.pos, Quaternion(0, 0, 0, 1), p.vel);
        static int logged = 0;
        if (logged++ < 3) ym::log("prisma: spawn %s ok=%d", g_itemKey, ok ? 1 : 0);
        if (!ok) { g_itemQueue.clear(); Notify("Item spawning isn't ready in this room"); }
    }
}

// The grid in front of you: 3 across, 3 deep, and 3 high for 3x3x3.
static void SpawnGrid(int count) {
    Il2CppObject *head = g_head.get();
    if (!head) return;
    Vector3 f = Flat(G.trGetForward[head]()), r = Cross(Vector3(0, 1, 0), f);
    Vector3 base = G.trGetPosition[head]() + f * 1.2f + Vector3(0, -0.4f, 0);
    std::vector<PendingItem> list;
    for (int y = 0; y < (count > 9 ? 3 : 1); y++)
        for (int z = 0; z < 3; z++)
            for (int x = 0; x < 3; x++)
                list.push_back({base + r * ((x - 1) * 0.45f) + f * (z * 0.45f) + Vector3(0, y * 0.45f, 0), Vector3(0, 0, 0)});
    QueueItems(list);
}
// The item you are holding right now, in either hand (the one closest to a hand wins).
// One scene search per press.
static bool HeldItem() {
    Il2CppObject *hands[2] = {g_leftHand.get(), g_rightHand.get()};
    if (!hands[0] || !hands[1] || !G.tItem || !G.itemKey.IsValid() || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return false;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)G.tItem)
                                                  : G.findSceneByType((Il2CppObject *)G.tItem, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    Vector3 hp[2] = {G.trGetPosition[hands[0]](), G.trGetPosition[hands[1]]()};
    Il2CppObject *best = nullptr;
    float bestD = 0.4f * 0.4f;
    for (size_t i = 0; i < n; i++) {
        Il2CppObject *it = ((Il2CppObject **)(arr + 0x20))[i];
        if (!Alive(it) || (G.itemHeld.IsValid() && !G.itemHeld[it]())) continue;
        Vector3 ip = G.trGetPosition[G.compTransform[it]()]();
        for (int h = 0; h < 2; h++) {
            Vector3 d = ip - hp[h];
            float dd = d.x * d.x + d.y * d.y + d.z * d.z;
            if (dd < bestD) { bestD = dd; best = it; }
        }
    }
    auto *key = best ? G.itemKey[best]() : nullptr;
    if (!key) { Notify("Hold an item in a hand first"); return false; }
    snprintf(g_itemKey, sizeof g_itemKey, "%s", key->str().c_str());
    return true;
}
static void ArmDupe9() { if (HeldItem()) SpawnGrid(9); }
static void ArmDupe27() { if (HeldItem()) SpawnGrid(27); }

// ---- item layouts: pictures made of the item in your hand -------------------------
// ASCII art, top row first, '#' = one item. The game's batch limit is 27 items, so no
// layout (or part of one) has more. A word too big for one batch is split into parts that
// you spawn with separate presses: each press is one batch with the usual 3 second wait.
struct Schematic {
    const char *name;
    std::vector<const char *> rows;
    std::vector<int> splits;                  // column boundaries of the parts; empty = one part
};
static const Schematic kSchematics[] = {
    {"Tree", {"....#....", "...###...", "..#####..", ".#######.", "....#....", "....#...."}, {}},
    {"House", {"...#...", "..###..", ".#####.", "#######", "#.....#", "#.....#", "##...##"}, {}},
    {"Heart", {".##.##.", "#######", ".#####.", ".#####.", "..###..", "...#..."}, {}},
    {"Smiley", {"..###..", ".#...#.", "#.#.#.#", "#.....#", "#.#.#.#", ".#.#.#.", "..###.."}, {}},
    {"Circle", {"..###..", ".#...#.", "#.....#", "#.....#", "#.....#", ".#...#.", "..###.."}, {}},
    {"Diamond", {"...#...", "..###..", ".#####.", "..###..", "...#..."}, {}},
    {"Pyramid", {"...#...", "..###..", ".#####.", "#######"}, {}},
    {"Plus", {"..#..", "..#..", "#####", "..#..", "..#.."}, {}},
    {"Wall", {"#####", "#####", "#####", "#####", "#####"}, {}},
    {"Right Arrow", {"....#...", "....##..", "########", "....##..", "....#..."}, {}},
    {"Left Arrow", {"...#....", "..##....", "########", "..##....", "...#...."}, {}},
    {"Up Arrow", {"..#..", ".###.", "#####", "..#..", "..#..", "..#..", "..#..", "..#.."}, {}},
    {"Down Arrow", {"..#..", "..#..", "..#..", "..#..", "..#..", "#####", ".###.", "..#.."}, {}},
    {"Hi", {"#.#.###", "#.#..#.", "###..#.", "#.#..#.", "#.#.###"}, {}},
    // HE = 21 items, LLO = 26 items
    {"Hello", {"#.#.###.#...#...###", "#.#.#...#...#...#.#", "###.##..#...#...#.#", "#.#.#...#...#...#.#",
               "#.#.###.###.###.###"}, {0, 8, 19}},
};
static_assert(sizeof(kSchematics) / sizeof(kSchematics[0]) == L_COUNT, "one schematic per layout id");

static int g_layoutId = -1, g_layoutPart = 0;
static Vector3 g_anchor, g_anchorF, g_anchorR;
static const float kLayoutSpacing[] = {0.2f, 0.3f, 0.45f};
static const char *kLayoutSizeName[] = {"small", "medium", "large"};
static int g_layoutSize = 1;
static void NextLayoutSize() { g_layoutSize = (g_layoutSize + 1) % 3; Notify("Layout size: %s", kLayoutSizeName[g_layoutSize]); }

static void SpawnSchematic(int id) {
    const Schematic &sc = kSchematics[id];
    int parts = sc.splits.empty() ? 1 : (int)sc.splits.size() - 1;
    Il2CppObject *head = g_head.get();
    if (!head) return;
    if (g_time < g_nextBatch) { Notify("Wait a moment"); return; }
    if (!HeldItem()) return;
    bool next = parts > 1 && id == g_layoutId && g_layoutPart > 0 && g_layoutPart < parts;
    int part = next ? g_layoutPart : 0;
    if (!next) {                                              // a new picture: put it in front of you
        Vector3 f = Flat(G.trGetForward[head]()), hp = G.trGetPosition[head]();
        float ground = hp.y - 1.4f;
        if (G.raycast.IsValid()) {                            // find the floor under you
            int mask = G.locomotionLayers.IsValid() && g_player.get() ? G.locomotionLayers[g_player.get()]() : 0;
            BNM::Structures::Unity::RaycastHit hit{};
            if (G.raycast(hp, Vector3(0, -1, 0), &hit, 4.0f, mask ? mask : -5)) ground = hit.point.y;
        }
        g_anchorF = f;
        g_anchorR = Cross(Vector3(0, 1, 0), f);
        g_anchor = Vector3(hp.x, ground + 0.25f, hp.z) + f * 2.2f;
    }
    int width = 0, nrow = (int)sc.rows.size();
    for (auto *row : sc.rows) width = std::max(width, (int)strlen(row));
    float cu = (width - 1) * 0.5f, sp = kLayoutSpacing[g_layoutSize];
    int c0 = sc.splits.empty() ? 0 : sc.splits[part], c1 = sc.splits.empty() ? width : sc.splits[part + 1];
    std::vector<PendingItem> list;
    for (int r = 0; r < nrow; r++)
        for (int c = c0; c < c1 && c < (int)strlen(sc.rows[r]); c++) {
            if (sc.rows[r][c] != '#') continue;
            float u = (c - cu) * sp, v = (nrow - 1 - r) * sp;
            Vector3 pos = g_anchor + g_anchorR * u + (f_layoutFloor ? g_anchorF * v : Vector3(0, v, 0));
            list.push_back({pos, Vector3(0, 0, 0)});
        }
    if (list.size() > 27) list.resize(27);                    // never more than one batch
    if (!QueueItems(list)) return;
    if (parts > 1) {
        g_layoutId = id;
        g_layoutPart = part + 1;
        if (g_layoutPart >= parts) { g_layoutId = -1; g_layoutPart = 0; Notify("%s done", sc.name); }
        else Notify("%s: part %d of %d, press again for the next", sc.name, part + 1, parts);
    } else {
        g_layoutId = -1;
        g_layoutPart = 0;
        Notify("Spawning %s", sc.name);
    }
}
#define X(n) static void Lay##n() { SpawnSchematic(L_##n); }
LAYOUT_LIST(X)
#undef X


// Spawn On Hand: each right-trigger press (grip open) spawns the item at your hand.
static bool g_onHandTrigger = false;
static void SpawnOnHandTick(const Buttons &in) {
    bool press = in.rtrig && !g_onHandTrigger;
    g_onHandTrigger = in.rtrig;
    Il2CppObject *hand = g_rightHand.get();
    if (!f_spawnOnHand || !press || in.rgrip || g_menuShown || !hand || g_time < g_nextItem) return;
    if (!HeldItem()) return;
    g_nextItem = g_time + 0.3;
    g_itemQueue.push_back({G.trGetPosition[hand]() + G.trGetForward[hand]() * 0.1f, Vector3(0, 0, 0)});
}

// ---- Prisma additions: boost, long arms, ropes, look ----------------------------------
// All of these only change your own game: your own speed, your own hands.

static Vector3 g_prevVel;                        // your speed at the end of the last frame
static bool g_havePrevVel = false;
static bool g_onRope = false;
static double g_nextRopeCheck = 0;
static bool g_ropeLogged = false;
static bool g_armsHave = false, g_armsApplied = false;
static Vector3 g_trueL, g_trueR, g_wroteL, g_wroteR;

static const float kBoostMult[] = {1.5f, 2.0f, 3.0f};
static const float kBoostCap[] = {10.0f, 14.0f, 20.0f};
static const char *kBoostName[] = {"mild", "strong", "extreme"};
static int g_boostLevel = 0;
static const float kRopeMult[] = {2.0f, 3.0f, 5.0f};
static const char *kRopeName[] = {"2x", "3x", "5x"};
static int g_ropePower = 1;
static const float kArmBoost[] = {0.1f, 0.2f, 0.3f, 0.4f};       // +20% is the default
static int g_armLevel = 1;

static void SpeedBoostToggled() { g_havePrevVel = false; }
static void NextBoost() { g_boostLevel = (g_boostLevel + 1) % 3; Notify("Boost strength: %s", kBoostName[g_boostLevel]); }
static void NextArm() { g_armLevel = (g_armLevel + 1) % 4; Notify("Arms +%d%%", (int)(kArmBoost[g_armLevel] * 100 + 0.5f)); }
static void NextRopePower() { g_ropePower = (g_ropePower + 1) % 3; Notify("Rope power: %s", kRopeName[g_ropePower]); }
static void NextTitleStyle() { g_titleStyle = (g_titleStyle + 1) % 4; g_nextTitle = 0; Notify("Title: %s", kTitleStyleName[g_titleStyle]); }
static void NextMenuSize() { g_menuSize = (g_menuSize + 1) % 3; g_sizeDirty = true; Notify("Menu size: %s", kMenuSizeName[g_menuSize]); }
static void NextPointerSize() {
    g_pointerSize = (g_pointerSize + 1) % 3;
    if (Alive(g_pointer.get())) {
        float p = kPointerSizes[g_pointerSize];
        G.trSetLocalScale[Tr(g_pointer.get())](Vector3(p, p, p));
    }
    Notify("Pointer size %d", g_pointerSize + 1);
}
static void ApplyLook() {                                    // italic on or off, right now
    if (!G.textSetFontStyle.IsValid()) return;
    int style = f_italic ? 2 : 0;
    for (Ref *r : {&g_title, &g_tipText, &g_notifyText})
        if (Alive(r->get())) G.textSetFontStyle[r->get()](style);
    for (int i = 0; i < kRows; i++)
        if (Alive(g_rowText[i].get())) G.textSetFontStyle[g_rowText[i].get()](style);
}

static void ResetMotionState() {
    g_havePrevVel = false;
    g_onRope = false;
    g_armsHave = false;
    g_armsApplied = false;
    g_layoutId = -1;
    g_layoutPart = 0;
    V2Reset();
}

// ---- Prisma V2: speed boost, rope boost, long arms, soundboard, ESP, world -----------------
// Everything here only changes your own game (and the soundboard plays through your own mic).

static std::string ShortType(const BNM::IL2CPP::Il2CppType *t) {
    std::string n = TypeName(t);
    size_t dot = n.rfind('.');
    return dot == std::string::npos ? n : n.substr(dot + 1);
}
static float Len(Vector3 v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }
static Quaternion QInv(Quaternion q) { return Quaternion(-q.x, -q.y, -q.z, q.w); }
static Vector3 QRot(Quaternion q, Vector3 v) {
    Vector3 u(q.x, q.y, q.z);
    Vector3 t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}
static Quaternion YawQ(float a) { return Quaternion(0, sinf(a * 0.5f), 0, cosf(a * 0.5f)); }

// ---- Speed Boost -------------------------------------------------------------------
// Two parts. (1) The game's own jump numbers are raised while it is on (put back when it
// is off). (2) Whatever way the game moves you, a share of that movement is added again, so
// you travel faster no matter how the game does it: each frame we look at how far you
// moved and push you a bit further the same way. We remember where we left you, so our own
// push is never counted twice. Capped, and off while another movement mod moves you.
static const float kJumpMult[] = {1.3f, 1.6f, 2.2f};
static float g_oldJumpSpeed = 0, g_oldJumpMultiplier = 0;
static Il2CppObject *g_jumpBoosted = nullptr;
static void JumpFields(bool on) {
    Il2CppObject *player = g_player.get();
    if (!player || !G.maxJumpSpeed.IsValid() || !G.jumpMultiplier.IsValid()) return;
    if (on) {
        if (g_jumpBoosted != player) {
            g_oldJumpSpeed = G.maxJumpSpeed[player]();
            g_oldJumpMultiplier = G.jumpMultiplier[player]();
            g_jumpBoosted = player;
        }
        G.maxJumpSpeed[player] = g_oldJumpSpeed * kJumpMult[g_boostLevel];
        G.jumpMultiplier[player] = g_oldJumpMultiplier * kJumpMult[g_boostLevel];
    } else if (g_jumpBoosted == player) {
        G.maxJumpSpeed[player] = g_oldJumpSpeed;
        G.jumpMultiplier[player] = g_oldJumpMultiplier;
        g_jumpBoosted = nullptr;
    }
}
static void SpeedBoostTick(const Buttons &in, float dt) {
    JumpFields(f_speedBoost);
    Il2CppObject *t = PlayerTransform();
    if (!f_speedBoost || !t || dt <= 0) { g_havePrevVel = false; return; }
    Vector3 cur = G.trGetPosition[t]();                      // g_prevVel = where we left you last frame
    bool other = (in.a && (f_fly || f_car || f_velocityFly || f_ironMan)) || (in.x && f_ironMan) ||
                 (f_noclip && in.rtrig) || g_onRope || g_dancing;
    if (g_havePrevVel && !other) {
        Vector3 d = cur - g_prevVel;
        float h = sqrtf(d.x * d.x + d.z * d.z), speed = h / dt;
        if (speed > 0.3f && speed < 20.0f) {                 // real movement, not a teleport or a room load
            float k = kBoostMult[g_boostLevel] - 1.0f;
            float cap = kBoostCap[g_boostLevel];
            float extra = fminf(h * k, 0.25f);               // at most 25 cm extra in one frame
            if ((h + extra) / dt > cap) extra = fmaxf(0.0f, cap * dt - h);
            Vector3 nv = cur + Vector3(d.x / h * extra, 0, d.z / h * extra);
            if (d.y > 0 && d.y / dt < 12.0f) nv.y += fminf(d.y * k, 0.1f);   // a jump goes higher too
            G.trSetPosition[t](nv);
            cur = nv;
        }
    }
    g_prevVel = cur;
    g_havePrevVel = true;
}

// ---- speedometer (shown in the menu title) ------------------------------------------
static Vector3 g_speedoPrev;
static bool g_speedoHave = false;
static void SpeedoTick(float dt) {
    Il2CppObject *t = PlayerTransform();
    if (!f_speedo || !t || dt <= 0) { g_speedoHave = false; return; }
    Vector3 p = G.trGetPosition[t]();
    if (g_speedoHave) {
        float s = Len(p - g_speedoPrev) / dt;
        g_speedNow += (s - g_speedNow) * fminf(1.0f, dt * 6.0f);
    }
    g_speedoPrev = p;
    g_speedoHave = true;
}

// ---- Rope Boost ----------------------------------------------------------------------
// A rope is found without knowing the game's rope code: the thing your hand grips is a rope
// if it (or something above it) has "rope" or "vine" in its name, or if it hangs on a
// physics joint (ropes and chains are made of joints). While a grip is held on one, your
// swing is pumped: the speed you already have keeps growing, up to a cap. If the game
// moves you along the rope by position instead of speed, that movement is stretched
// instead. A small physics query ten times a second, only while a grip is held.
static bool NameHasRope(Il2CppObject *comp) {
    Il2CppObject *t = G.compTransform[comp]();
    for (int up = 0; up < 4 && t; up++, t = G.trGetParent.IsValid() ? G.trGetParent[t]() : nullptr) {
        Il2CppObject *go = G.compGameObject[t]();
        auto *s = go ? G.objGetName[go]() : nullptr;
        if (!s) continue;
        std::string n = s->str();
        for (auto &ch : n) ch = (char)tolower((unsigned char)ch);
        if (n.find("rope") != std::string::npos || n.find("vine") != std::string::npos) {
            if (!g_ropeLogged) { g_ropeLogged = true; ym::log("prisma: rope found: %s", s->str().c_str()); }
            return true;
        }
    }
    return false;
}
static bool RopeLike(Il2CppObject *c) {
    if (NameHasRope(c)) return true;
    if (!G.colAttachedRb.IsValid() || !G.tJoint) return false;
    Il2CppObject *rb = G.colAttachedRb[c]();
    if (!Alive(rb) || rb == g_rigidbody.get()) return false;         // never your own body
    Il2CppObject *go = G.compGameObject[rb]();
    Il2CppObject *joint = go ? G.goGetComponent[go]((Il2CppObject *)G.tJoint) : nullptr;
    if (!Alive(joint)) return false;
    if (!g_ropeLogged) { g_ropeLogged = true; ym::log("prisma: rope-like object found (hangs on a joint)"); }
    return true;
}
static bool HandOnRope(Il2CppObject *hand) {
    auto *arr = (uint8_t *)G.overlapSphere(G.trGetPosition[hand](), 0.25f);
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    for (size_t i = 0; i < n && i < 16; i++) {
        Il2CppObject *c = ((Il2CppObject **)(arr + 0x20))[i];
        if (Alive(c) && RopeLike(c)) return true;
    }
    return false;
}
static Vector3 g_ropePrevPos;
static bool g_ropeHavePos = false;
static void RopeTick(const Buttons &in, float dt) {
    Il2CppObject *t = PlayerTransform();
    if (!f_rope || !t || dt <= 0 || !G.overlapSphere.IsValid() || !G.objGetName.IsValid() ||
        !(in.lgrip || in.rgrip) || !g_leftHand.get() || !g_rightHand.get()) {
        g_onRope = false;
        g_ropeHavePos = false;
        return;
    }
    if (g_time >= g_nextRopeCheck) {
        g_nextRopeCheck = g_time + 0.1;
        g_onRope = (in.lgrip && HandOnRope(g_leftHand.get())) || (in.rgrip && HandOnRope(g_rightHand.get()));
    }
    if (!g_onRope) { g_ropeHavePos = false; return; }
    float k = (kRopeMult[g_ropePower] - 1.0f) * 0.6f;
    Vector3 pos = G.trGetPosition[t](), v;
    if (GetVel(v) && Len(v) > 0.5f) {
        Vector3 nv = v * (1.0f + k * dt);
        float l = Len(nv);
        if (l > 22.0f) nv = nv * (22.0f / l);
        SetVel(nv);
    } else if (g_ropeHavePos) {
        Vector3 d = pos - g_ropePrevPos;
        float sp = Len(d) / dt;
        if (sp > 0.5f && sp < 15.0f) { pos = pos + d * (k * dt * 4.0f); G.trSetPosition[t](pos); }
    }
    g_ropePrevPos = pos;
    g_ropeHavePos = true;
}

// ---- Long Arms, three ways ------------------------------------------------------------
// 0 = the game's own arm-length number, if the player has one (found by name, once);
// 1 = tracker: your hands are driven from the controllers' real positions, so nothing can
//     put them back (the same way the dances move your hands);
// 2 = simple: the hands are pushed out each frame after the game placed them.
// Auto picks 0 when the number exists, otherwise 1, and falls back to 2 if 1 fails.
static int g_armMethod = 0;
static const char *kArmMethodName[] = {"auto", "tracker", "simple"};
static void NextArmMethod() {
    g_armMethod = (g_armMethod + 1) % 3;
    g_trackerBad = false;
    Notify("Arm method: %s", kArmMethodName[g_armMethod]);
}

static Field<float> g_armFieldF;
static bool g_armFieldOk = false, g_armFieldSearched = false;
static float g_armOrig = 0;
static Il2CppObject *g_armFieldPlayer = nullptr;
static bool FindArmField() {
    if (g_armFieldSearched) return g_armFieldOk;
    g_armFieldSearched = true;
    Class player("", "MasterPlayer", Image("Assembly-CSharp.dll"));
    if (!player.IsValid()) return false;
    for (auto &f : player.GetFields(true)) {
        auto *fi = f.GetInfo();
        if (!fi || !fi->name || ShortType(fi->type) != "Single") continue;
        std::string n = fi->name, low = n;
        for (auto &ch : low) ch = (char)tolower((unsigned char)ch);
        bool arm = (n.compare(0, 3, "arm") == 0 || n.find("Arm") != std::string::npos || n.find("_arm") != std::string::npos) &&
                   low.find("armor") == std::string::npos && low.find("army") == std::string::npos;
        bool size = low.find("length") != std::string::npos || low.find("reach") != std::string::npos ||
                    low.find("scale") != std::string::npos || low.find("extend") != std::string::npos ||
                    low.find("size") != std::string::npos || low.find("mult") != std::string::npos;
        if (arm && size) {
            g_armFieldF = f;
            g_armFieldOk = true;
            ym::log("prisma: long arms: using the game's own number %s", fi->name);
            break;
        }
    }
    return g_armFieldOk;
}
static void ArmsFieldTick(bool on) {
    Il2CppObject *player = g_player.get();
    if (!g_armFieldOk || !player) return;
    if (on) {
        if (g_armFieldPlayer != player) { g_armOrig = g_armFieldF[player](); g_armFieldPlayer = player; }
        if (g_armOrig > 0.001f) g_armFieldF[player] = g_armOrig * (1.0f + kArmBoost[g_armLevel]);
    } else if (g_armFieldPlayer == player) {
        g_armFieldF[player] = g_armOrig;
        g_armFieldPlayer = nullptr;
    }
}

static Ref g_usePos, g_useRot;
static bool ReadPose(int node, Vector3 &p, Quaternion &q) {
    XRDevice d = G.deviceAtNode(node);
    if (!d.initialized || !G.tryGetVec3.IsValid() || !G.tryGetQuat.IsValid()) return false;
    if (!g_usePos.get()) {
        g_usePos.set((Il2CppObject *)CreateMonoString("DevicePosition"));
        g_useRot.set((Il2CppObject *)CreateMonoString("DeviceRotation"));
    }
    bool a = G.tryGetVec3(d.id, (BNM::Structures::Mono::String *)g_usePos.get(), &p);
    bool b = G.tryGetQuat(d.id, (BNM::Structures::Mono::String *)g_useRot.get(), &q);
    return a && b;
}
struct HandCal { Vector3 pOff = Vector3(0, 0, 0); Quaternion qOff = Quaternion(0, 0, 0, 1); };
static HandCal g_cal[2];
static void TrackerFail(const char *why) {
    g_trackerBad = true;
    if (g_armsTracker && !g_dancing) PauseTrackers(false);
    g_armsTracker = false;
    ym::log("prisma: long arms tracker mode failed: %s", why);
    Notify("Long Arms: using the simple way");
}
static void ArmsTrackerTick() {
    Il2CppObject *head = g_head.get(), *hands[2] = {g_leftHand.get(), g_rightHand.get()};
    Vector3 headDevP(0, 0, 0), headW = G.trGetPosition[head]();
    Quaternion hq(0, 0, 0, 1);
    if (!ReadPose(3, headDevP, hq)) { TrackerFail("controller data unavailable"); return; }
    Vector3 fw = G.trGetForward[head](), fd = QRot(hq, Vector3(0, 0, 1));
    if (fw.x * fw.x + fw.z * fw.z < 0.01f || fd.x * fd.x + fd.z * fd.z < 0.01f) return;   // looking straight up or down
    Quaternion yawQ = YawQ(atan2f(fw.x, fw.z) - atan2f(fd.x, fd.z));
    Vector3 pred[2] = {Vector3(0, 0, 0), Vector3(0, 0, 0)};
    Quaternion devW[2] = {Quaternion(0, 0, 0, 1), Quaternion(0, 0, 0, 1)};
    for (int i = 0; i < 2; i++) {
        Vector3 p(0, 0, 0);
        Quaternion q(0, 0, 0, 1);
        if (!ReadPose(i == 0 ? 4 : 5, p, q)) { TrackerFail("controller data unavailable"); return; }
        devW[i] = yawQ * q;
        pred[i] = headW + QRot(yawQ, p - headDevP);
    }
    if (!g_armsTracker) {                                    // first frame: learn how the hand sits on the controller
        for (int i = 0; i < 2; i++) {
            Vector3 off = G.trGetPosition[hands[i]]() - pred[i];
            if (Len(off) > 0.6f) { TrackerFail("controller position does not match the hand"); return; }
            g_cal[i].pOff = QRot(QInv(devW[i]), off);
            g_cal[i].qOff = QInv(devW[i]) * G.trGetRotation[hands[i]]();
        }
        PauseTrackers(true);
        g_armsTracker = true;
        ym::log("prisma: long arms: tracker mode on");
    }
    Vector3 up(0, 1, 0), f = Flat(G.trGetForward[head]()), r = Cross(up, f);
    float s = 1.0f + kArmBoost[g_armLevel];
    for (int i = 0; i < 2; i++) {
        Vector3 truePos = pred[i] + QRot(devW[i], g_cal[i].pOff);
        Vector3 sh = headW + up * -0.25f + r * (i == 0 ? -0.18f : 0.18f);
        Vector3 out = sh + (truePos - sh) * s;
        if (Len(out - headW) > 1.8f) { TrackerFail("hand ended up too far away"); return; }
        G.trSetPosition[hands[i]](out);
        G.trSetRotation[hands[i]](devW[i] * g_cal[i].qOff);
    }
}

static void ArmsSimpleTick() {
    Il2CppObject *head = g_head.get(), *lh = g_leftHand.get(), *rh = g_rightHand.get();
    Vector3 hp = G.trGetPosition[head](), f = Flat(G.trGetForward[head]()), up(0, 1, 0);
    Vector3 r = Cross(up, f);
    Vector3 shL = hp + up * -0.25f - r * 0.18f, shR = hp + up * -0.25f + r * 0.18f;
    Vector3 curL = G.trGetPosition[lh](), curR = G.trGetPosition[rh]();
    auto same = [](Vector3 a, Vector3 b) {
        Vector3 d = a - b;
        return d.x * d.x + d.y * d.y + d.z * d.z < 1e-8f;
    };
    if (!g_armsHave || !same(curL, g_wroteL)) g_trueL = curL;    // moved by the controller: that is the real spot
    if (!g_armsHave || !same(curR, g_wroteR)) g_trueR = curR;
    float s = 1.0f + kArmBoost[g_armLevel];
    g_wroteL = shL + (g_trueL - shL) * s;
    g_wroteR = shR + (g_trueR - shR) * s;
    G.trSetPosition[lh](g_wroteL);
    G.trSetPosition[rh](g_wroteR);
    g_armsHave = true;
    g_armsApplied = true;
}
static void ArmsSimpleOff() {
    Il2CppObject *lh = g_leftHand.get(), *rh = g_rightHand.get();
    if (g_armsApplied && g_armsHave && !g_dancing && Alive(lh) && Alive(rh)) {
        G.trSetPosition[lh](g_trueL);
        G.trSetPosition[rh](g_trueR);
    }
    g_armsApplied = false;
    g_armsHave = false;
}

static int g_armActive = -1;                                 // which way is running: 0, 1, 2 or -1 = off
static void ArmsCleanup(int m) {
    if (m == 0) ArmsFieldTick(false);
    else if (m == 1) {
        if (g_armsTracker && !g_dancing) PauseTrackers(false);
        g_armsTracker = false;
    } else if (m == 2) ArmsSimpleOff();
}
static void ArmsTick() {
    int eff = -1;
    if (f_longArms && !g_dancing && g_head.get() && g_leftHand.get() && g_rightHand.get()) {
        int m = g_armMethod;
        if (m == 0) m = FindArmField() ? 0 : 1;
        if (m == 1 && g_trackerBad) m = 2;
        eff = m;
    }
    if (eff != g_armActive) { ArmsCleanup(g_armActive); g_armActive = eff; }
    if (eff == 0) ArmsFieldTick(true);
    else if (eff == 1) ArmsTrackerTick();
    else if (eff == 2) ArmsSimpleTick();
}

// ---- Soundboard ------------------------------------------------------------------------
// Put .mp3 or .wav files in a folder called "sounds" inside the mods folder (loose files in
// the mods folder work too). Pick one in the Soundboard page and it plays through your own
// microphone channel, so the room hears it like a real soundboard. Owner rules (AGENTS.md):
// one sound at a time; the button stays locked until the sound has finished, and never
// under 1 second; normal voice volume (no gain, no looping, no stacking); 30 s at most.
// The file is read, decoded and loaded a little each frame so the game never stalls.
static std::vector<std::string> g_soundFiles, g_soundLabels;
static constexpr int kMaxSounds = 40;
static constexpr size_t kMaxSoundFile = 20u * 1024 * 1024;
static constexpr float kMaxSoundSeconds = 30.0f;

enum SndState { SND_IDLE, SND_READ, SND_DECODE, SND_MAKE, SND_START, SND_PLAY };
static struct {
    SndState st = SND_IDLE;
    FILE *fp = nullptr;
    size_t size = 0, pos = 0, wavEnd = 0;
    bool isWav = false, saved = false, oldLoop = false;
    int hz = 0, ch = 1, oldSource = 0;
    std::vector<uint8_t> file;
    std::vector<float> pcm;
    mp3dec_t dec;
    double busyUntil = 0, endAt = 0;
    float seconds = 0;
    Ref clip, recorder, oldClip;
} g_snd;

static struct {
    bool tried = false, ok = false, restartHasArg = false;
    Method<Il2CppObject *> clipCreate, recGetClip;
    Method<bool> clipSetData, recGetLoop;
    Method<void> recSetSource, recSetClip, recSetLoop, recRestart;
    Method<int> recGetSource;
    BNM::MonoType *tRecorder = nullptr;
    BNM::IL2CPP::Il2CppClass *floatClass = nullptr;
} g_vox;

static bool VoiceLookups() {
    if (g_vox.tried) return g_vox.ok;
    g_vox.tried = true;
    Class rec("Photon.Voice.Unity", "Recorder"), clip("UnityEngine", "AudioClip");
    if (!rec.IsValid() || !clip.IsValid()) { ym::log("prisma: voice classes missing (recorder %d, clip %d)", rec.IsValid(), clip.IsValid()); return false; }
    g_vox.tRecorder = rec.GetMonoType();
    g_vox.recSetSource = rec.GetMethod("set_SourceType", 1);
    g_vox.recGetSource = rec.GetMethod("get_SourceType", 0);
    g_vox.recSetClip = rec.GetMethod("set_AudioClip", 1);
    g_vox.recGetClip = rec.GetMethod("get_AudioClip", 0);
    g_vox.recSetLoop = rec.GetMethod("set_LoopAudioClip", 1);
    g_vox.recGetLoop = rec.GetMethod("get_LoopAudioClip", 0);
    g_vox.recRestart = rec.GetMethod("RestartRecording", 1);
    g_vox.restartHasArg = g_vox.recRestart.IsValid();
    if (!g_vox.restartHasArg) g_vox.recRestart = rec.GetMethod("RestartRecording", 0);
    g_vox.clipCreate = clip.GetMethod("Create", 5);
    g_vox.clipSetData = clip.GetMethod("SetData", 2);
    g_vox.floatClass = Class("System", "Single").GetClass();
    g_vox.ok = g_vox.tRecorder && g_vox.recSetSource.IsValid() && g_vox.recGetSource.IsValid() && g_vox.recSetClip.IsValid() &&
               g_vox.recGetClip.IsValid() && g_vox.recSetLoop.IsValid() && g_vox.recGetLoop.IsValid() &&
               g_vox.recRestart.IsValid() && g_vox.clipCreate.IsValid() && g_vox.clipSetData.IsValid() &&
               g_vox.floatClass && G.arrayNew && (G.findScene.IsValid() || G.findSceneByType.IsValid());
    ym::log("prisma: voice lookups ok=%d (source %d/%d, clip %d/%d, loop %d/%d, restart %d, create %d, setdata %d)", g_vox.ok,
            g_vox.recSetSource.IsValid(), g_vox.recGetSource.IsValid(), g_vox.recSetClip.IsValid(), g_vox.recGetClip.IsValid(),
            g_vox.recSetLoop.IsValid(), g_vox.recGetLoop.IsValid(), g_vox.recRestart.IsValid(), g_vox.clipCreate.IsValid(),
            g_vox.clipSetData.IsValid());
    return g_vox.ok;
}
static Il2CppObject *FindRecorder() {
    Il2CppObject *r = g_snd.recorder.get();
    if (Alive(r)) return r;
    if (!g_vox.tRecorder) return nullptr;
    auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)g_vox.tRecorder)
                                                  : G.findSceneByType((Il2CppObject *)g_vox.tRecorder, 0));
    size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
    if (n == 0) return nullptr;
    Il2CppObject *best = ((Il2CppObject **)(arr + 0x20))[0];
    g_snd.recorder.set(best);
    return best;
}

static void SoundClear() {
    if (g_snd.fp) { fclose(g_snd.fp); g_snd.fp = nullptr; }
    std::vector<uint8_t>().swap(g_snd.file);
    std::vector<float>().swap(g_snd.pcm);
}
static void SoundRestore() {                                 // give the microphone back
    Il2CppObject *rec = g_snd.recorder.get();
    if (g_snd.saved && Alive(rec)) {
        try {
            g_vox.recSetLoop[rec](g_snd.oldLoop);
            g_vox.recSetClip[rec](g_snd.oldClip.get());
            g_vox.recSetSource[rec](g_snd.oldSource);
            if (g_vox.restartHasArg) g_vox.recRestart[rec](true); else g_vox.recRestart[rec]();
        } catch (...) { ym::log("prisma: sound: restore error"); }
    }
    g_snd.saved = false;
    g_snd.oldClip.clear();
    Kill(g_snd.clip);
}
static void SoundFail(const char *msg) {
    SoundClear();
    g_snd.st = SND_IDLE;
    g_snd.busyUntil = g_time + 1.0;
    Notify("%s", msg);
    ym::log("prisma: sound: %s", msg);
}
static void SoundAbort() {
    SoundClear();
    if (g_snd.st != SND_IDLE || g_snd.saved) SoundRestore();
    g_snd.st = SND_IDLE;
    g_snd.recorder.clear();
}

static uint32_t Le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t Le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static bool WavInit() {                                      // 16-bit PCM only
    const uint8_t *d = g_snd.file.data();
    size_t n = g_snd.file.size(), pos = 12, start = 0, end = 0;
    if (n < 44 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return false;
    int fmt = 0, ch = 0, bits = 0, hz = 0;
    while (pos + 8 <= n) {
        uint32_t sz = Le32(d + pos + 4);
        if (memcmp(d + pos, "fmt ", 4) == 0 && pos + 24 <= n) {
            fmt = Le16(d + pos + 8); ch = Le16(d + pos + 10); hz = (int)Le32(d + pos + 12); bits = Le16(d + pos + 22);
        } else if (memcmp(d + pos, "data", 4) == 0) {
            start = pos + 8;
            end = std::min<size_t>(n, start + sz);
            break;
        }
        pos += 8 + (size_t)sz + (sz & 1);
    }
    if (fmt != 1 || bits != 16 || ch < 1 || ch > 2 || hz < 8000 || start == 0) return false;
    g_snd.hz = hz; g_snd.ch = ch; g_snd.pos = start; g_snd.wavEnd = end;
    return true;
}

static void SoundPlay(int idx) {
    if (idx < 0 || idx >= (int)g_soundFiles.size()) return;
    if (g_snd.st != SND_IDLE || g_time < g_snd.busyUntil) { Notify("Wait for the sound to finish"); return; }
    if (!VoiceLookups()) { Notify("Voice chat not found in this game"); return; }
    const std::string &path = g_soundFiles[idx];
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) { Notify("Could not open that file"); return; }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len < 100 || (size_t)len > kMaxSoundFile) {
        fclose(fp);
        Notify(len < 100 ? "That file is empty" : "File too big (20 MB max)");
        return;
    }
    g_snd.fp = fp;
    g_snd.size = (size_t)len;
    g_snd.file.clear();
    g_snd.file.reserve(g_snd.size);
    g_snd.pcm.clear();
    g_snd.pos = 0;
    g_snd.hz = 0;
    std::string low = path;
    for (auto &c : low) c = (char)tolower((unsigned char)c);
    g_snd.isWav = low.size() > 4 && low.compare(low.size() - 4, 4, ".wav") == 0;
    g_snd.st = SND_READ;
    g_snd.busyUntil = g_time + 1.0;
    Notify("Loading sound...");
}

static void SoundTick() {
    if (g_snd.st == SND_IDLE) return;
    try {
        switch (g_snd.st) {
        case SND_READ: {                                     // 512 KB a frame
            size_t have = g_snd.file.size(), want = std::min<size_t>(512 * 1024, g_snd.size - have);
            g_snd.file.resize(have + want);
            size_t got = fread(g_snd.file.data() + have, 1, want, g_snd.fp);
            if (got < want) { g_snd.file.resize(have + got); g_snd.size = g_snd.file.size(); }
            if (g_snd.file.size() >= g_snd.size) {
                fclose(g_snd.fp);
                g_snd.fp = nullptr;
                g_snd.pos = 0;
                if (g_snd.isWav) {
                    if (!WavInit()) { SoundFail("Only 16-bit .wav files work"); break; }
                } else mp3dec_init(&g_snd.dec);
                g_snd.pcm.reserve(48000 * (size_t)kMaxSoundSeconds + 4096);
                g_snd.st = SND_DECODE;
            }
            break;
        }
        case SND_DECODE: {                                   // at most about 1.5 ms of decoding a frame
            auto t0 = std::chrono::steady_clock::now();
            auto slow = [&]() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > 1.5; };
            bool done = false;
            if (g_snd.isWav) {
                while (g_snd.pos + 2 * g_snd.ch <= g_snd.wavEnd && !slow()) {
                    for (int n = 0; n < 2048 && g_snd.pos + 2 * g_snd.ch <= g_snd.wavEnd; n++) {
                        float acc = 0;
                        for (int c = 0; c < g_snd.ch; c++) {
                            int16_t v;
                            memcpy(&v, g_snd.file.data() + g_snd.pos, 2);
                            g_snd.pos += 2;
                            acc += v;
                        }
                        g_snd.pcm.push_back(acc / g_snd.ch / 32768.0f);
                    }
                }
                done = g_snd.pos + 2 * g_snd.ch > g_snd.wavEnd;
            } else {
                static mp3d_sample_t buf[MINIMP3_MAX_SAMPLES_PER_FRAME];
                mp3dec_frame_info_t info;
                while (!slow()) {
                    if (g_snd.pos >= g_snd.file.size()) { done = true; break; }
                    int n = mp3dec_decode_frame(&g_snd.dec, g_snd.file.data() + g_snd.pos, (int)(g_snd.file.size() - g_snd.pos), buf, &info);
                    if (info.frame_bytes <= 0) { done = true; break; }
                    g_snd.pos += info.frame_bytes;
                    if (n > 0 && info.channels > 0) {
                        g_snd.hz = info.hz;
                        for (int i = 0; i < n; i++) {
                            float acc = 0;
                            for (int c = 0; c < info.channels; c++) acc += buf[i * info.channels + c];
                            g_snd.pcm.push_back(acc / info.channels / 32768.0f);
                        }
                    }
                    if (g_snd.hz > 0 && g_snd.pcm.size() > (size_t)(g_snd.hz * kMaxSoundSeconds)) break;
                }
            }
            if (g_snd.hz > 0 && g_snd.pcm.size() > (size_t)(g_snd.hz * kMaxSoundSeconds)) { SoundFail("Sound is too long (30 s max)"); break; }
            if (done) {
                if (g_snd.hz <= 0 || g_snd.pcm.size() < (size_t)g_snd.hz / 10) { SoundFail("Could not read that sound"); break; }
                std::vector<uint8_t>().swap(g_snd.file);
                g_snd.st = SND_MAKE;
            }
            break;
        }
        case SND_MAKE: {                                     // the samples become a Unity AudioClip
            Il2CppObject *rec = FindRecorder();
            if (!rec) { SoundFail("Voice recorder not found"); break; }
            size_t n = g_snd.pcm.size();
            auto *arr = (uint8_t *)G.arrayNew((void *)g_vox.floatClass, (uintptr_t)n);
            if (!arr) { SoundFail("Out of memory"); break; }
            memcpy(arr + 0x20, g_snd.pcm.data(), n * sizeof(float));
            Il2CppObject *clip = g_vox.clipCreate(CreateMonoString("prisma"), (int)n, 1, g_snd.hz, false);
            if (!clip) { SoundFail("Could not make the sound"); break; }
            g_vox.clipSetData[clip]((Il2CppObject *)arr, 0);
            g_snd.clip.set(clip);
            g_snd.seconds = (float)n / (float)g_snd.hz;
            std::vector<float>().swap(g_snd.pcm);
            g_snd.st = SND_START;
            break;
        }
        case SND_START: {                                    // the recorder reads the clip instead of the mic
            Il2CppObject *rec = FindRecorder();
            if (!rec || !Alive(g_snd.clip.get())) { SoundFail("Voice recorder not found"); break; }
            g_snd.oldSource = g_vox.recGetSource[rec]();
            g_snd.oldClip.set(g_vox.recGetClip[rec]());
            g_snd.oldLoop = g_vox.recGetLoop[rec]();
            g_snd.saved = true;
            g_vox.recSetLoop[rec](false);
            g_vox.recSetClip[rec](g_snd.clip.get());
            g_vox.recSetSource[rec](1);                      // Recorder.InputSourceType.AudioClip
            if (g_vox.restartHasArg) g_vox.recRestart[rec](true); else g_vox.recRestart[rec]();
            g_snd.endAt = g_time + fmaxf(g_snd.seconds, 1.0f) + 0.5f;
            g_snd.busyUntil = g_snd.endAt;                   // locked until it has finished
            g_snd.st = SND_PLAY;
            Notify("Playing (%d s)", (int)(g_snd.seconds + 0.5f));
            break;
        }
        case SND_PLAY:
            if (g_time >= g_snd.endAt) { SoundRestore(); g_snd.st = SND_IDLE; }
            break;
        default: break;
        }
    } catch (...) {
        SoundFail("Sound error");
        SoundRestore();
    }
}

static void PlaySoundPressed() { SoundPlay(g_pressArg); }
static void ScanSounds() {
    g_soundFiles.clear();
    g_soundLabels.clear();
    g_soundItems.clear();
    const char *mods = ym::api()->modsDir;
    if (!mods) return;
    std::string base = mods;
    auto ends = [](const std::string &n, const char *e) {
        size_t l = strlen(e);
        if (n.size() < l) return false;
        for (size_t i = 0; i < l; i++) if (tolower((unsigned char)n[n.size() - l + i]) != e[i]) return false;
        return true;
    };
    std::vector<std::pair<std::string, std::string>> found;      // label, path
    for (std::string dir : {base + "/sounds", base}) {
        DIR *d = opendir(dir.c_str());
        if (!d) continue;
        while (struct dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n.empty() || n[0] == '.' || !(ends(n, ".mp3") || ends(n, ".wav"))) continue;
            std::string label = n.substr(0, n.size() - 4);
            if (label.size() > 22) label.resize(22);
            if (found.size() < (size_t)kMaxSounds) found.push_back({label, dir + "/" + n});
        }
        closedir(d);
    }
    std::sort(found.begin(), found.end());
    for (auto &f : found) { g_soundLabels.push_back(f.first); g_soundFiles.push_back(f.second); }
    g_soundItems.reserve(kMaxSounds);
    for (size_t i = 0; i < g_soundLabels.size(); i++)
        g_soundItems.push_back(Item{SOUNDS, g_soundLabels[i].c_str(), ACTION, MAIN, nullptr, PlaySoundPressed, (int)i});
}

// ---- ESP (only you see it) ------------------------------------------------------------
// A box and a line to every other player, drawn with the same line objects the teleport
// gun uses. Other players are found with one scene search every two seconds. Nothing is sent.
static constexpr int kEspMax = 12;
static BNM::MonoType *g_tMasterPlayer = nullptr;
static Ref g_espShader;
static bool g_espZ = false, g_espShaderTried = false;
static Ref g_espBoxGo[kEspMax], g_espBox[kEspMax], g_espTrGo[kEspMax], g_espTr[kEspMax], g_espTarget[kEspMax];
static int g_espN = 0;
static double g_nextEspScan = 0, g_espSaid = 0;

static void EspReset() {
    for (int i = 0; i < kEspMax; i++) {
        Kill(g_espBoxGo[i]); g_espBox[i].clear();
        Kill(g_espTrGo[i]); g_espTr[i].clear();
        g_espTarget[i].clear();
    }
    g_espN = 0;
    g_nextEspScan = 0;
}
static bool MakeEspLine(Ref &goOut, Ref &lrOut, int count, float width, Color c) {
    Il2CppObject *lineGo = Class("UnityEngine", "GameObject").CreateNewObjectParameters(CreateMonoString("PrismaEsp"));
    Il2CppObject *line = lineGo && G.tLine ? G.goAddComponent[lineGo](G.tLine) : nullptr;
    if (!line) return false;
    if (Il2CppObject *m = G.rendererGetMaterial[line]()) {
        Il2CppObject *sh = g_espShader.get() ? g_espShader.get() : G.shader.get();
        if (sh) G.matSetShader[m](sh);
        G.matSetColor[m](c);
        if (g_espZ && G.matSetInt.IsValid()) G.matSetInt[m](CreateMonoString("_ZTest"), 8);   // always on top, through walls
    }
    G.lrSetStartWidth[line](width);
    G.lrSetEndWidth[line](width);
    G.lrSetPositionCount[line](count);
    G.lrSetUseWorldSpace[line](true);
    goOut.set(lineGo);
    lrOut.set(line);
    return true;
}
static void EspTick() {
    static bool wasOn = false;
    if (!f_esp) {
        if (wasOn) { for (int i = 0; i < kEspMax; i++) { Show(g_espBoxGo[i], false); Show(g_espTrGo[i], false); } wasOn = false; }
        return;
    }
    wasOn = true;
    if (!G.lrSetPosition.IsValid() || !G.headTransform.IsValid()) return;
    if (!g_espShaderTried) {
        g_espShaderTried = true;
        const char *names[] = {"Hidden/Internal-Colored", "GUI/Text Shader", "Sprites/Default"};
        for (int i = 0; i < 3 && !g_espShader.get() && G.shaderFind.IsValid(); i++) {
            Il2CppObject *sh = G.shaderFind((BNM::Structures::Mono::String *)CreateMonoString(names[i]));
            if (Alive(sh)) { g_espShader.set(sh); g_espZ = (i == 0); }
        }
        ym::log("prisma: esp shader %s", g_espShader.get() ? (g_espZ ? "see-through" : "plain") : "default");
    }
    if (!g_tMasterPlayer) {
        Class mp("", "MasterPlayer", Image("Assembly-CSharp.dll"));
        if (mp.IsValid()) g_tMasterPlayer = mp.GetMonoType();
    }
    if (!g_tMasterPlayer || !(G.findScene.IsValid() || G.findSceneByType.IsValid())) return;
    if (g_time >= g_nextEspScan) {
        g_nextEspScan = g_time + 2.0;
        auto *arr = (uint8_t *)(G.findScene.IsValid() ? G.findScene((Il2CppObject *)g_tMasterPlayer)
                                                      : G.findSceneByType((Il2CppObject *)g_tMasterPlayer, 0));
        size_t n = arr ? *(uintptr_t *)(arr + 0x18) : 0;
        int k = 0;
        for (size_t i = 0; i < n && k < kEspMax; i++) {
            Il2CppObject *p = ((Il2CppObject **)(arr + 0x20))[i];
            if (Alive(p) && p != g_player.get()) g_espTarget[k++].set(p);
        }
        for (int i = k; i < kEspMax; i++) g_espTarget[i].clear();
        g_espN = k;
        if (k == 0 && g_time - g_espSaid > 8.0) { g_espSaid = g_time; Notify("ESP: nobody else found"); }
    }
    Il2CppObject *rh = g_rightHand.get();
    for (int i = 0; i < kEspMax; i++) {
        Il2CppObject *p = g_espTarget[i].get();
        if (i >= g_espN || !Alive(p)) { Show(g_espBoxGo[i], false); Show(g_espTrGo[i], false); continue; }
        Il2CppObject *ht = G.headTransform[p]();
        Vector3 head = Alive(ht) ? G.trGetPosition[ht]() : G.trGetPosition[G.compTransform[p]()]() + Vector3(0, 1.6f, 0);
        if (!g_espBox[i].get() && !MakeEspLine(g_espBoxGo[i], g_espBox[i], 16, 0.015f, T().title)) continue;
        Show(g_espBoxGo[i], true);
        const float w = 0.3f, cx[4] = {-w, w, w, -w}, cz[4] = {-w, -w, w, w};
        Vector3 b[4], t[4];
        for (int k = 0; k < 4; k++) {
            b[k] = Vector3(head.x + cx[k], head.y - 1.7f, head.z + cz[k]);
            t[k] = Vector3(head.x + cx[k], head.y + 0.15f, head.z + cz[k]);
        }
        const Vector3 pts[16] = {b[0], b[1], b[2], b[3], b[0], t[0], t[1], b[1], t[1], t[2], b[2], t[2], t[3], b[3], t[3], t[0]};
        Il2CppObject *lr = g_espBox[i].get();
        for (int k = 0; k < 16; k++) G.lrSetPosition[lr](k, pts[k]);
        if (f_espTracers && Alive(rh)) {
            if (!g_espTr[i].get() && !MakeEspLine(g_espTrGo[i], g_espTr[i], 2, 0.006f, T().bgA)) continue;
            Show(g_espTrGo[i], true);
            G.lrSetPosition[g_espTr[i].get()](0, G.trGetPosition[rh]());
            G.lrSetPosition[g_espTr[i].get()](1, head - Vector3(0, 0.5f, 0));
        } else Show(g_espTrGo[i], false);
    }
}

// ---- world looks: no fog, bright world -------------------------------------------------
static bool g_worldFogSaved = false, g_oldFog = false, g_worldAmbSaved = false;
static int g_oldAmbMode = 0;
static Color g_oldAmb = Color(1, 1, 1, 1);
static double g_nextWorld = 0;
static void WorldReset() { g_worldFogSaved = false; g_worldAmbSaved = false; g_nextWorld = 0; }
static void WorldTick() {
    if (g_time < g_nextWorld) return;
    g_nextWorld = g_time + 0.5;                               // checked twice a second, so scene loads can't undo it
    if (G.rsSetFog.IsValid() && G.rsGetFog.IsValid()) {
        if (f_noFog) {
            if (!g_worldFogSaved) { g_oldFog = G.rsGetFog(); g_worldFogSaved = true; }
            G.rsSetFog(false);
        } else if (g_worldFogSaved) { G.rsSetFog(g_oldFog); g_worldFogSaved = false; }
    }
    if (G.rsSetAmbientMode.IsValid() && G.rsSetAmbientLight.IsValid() && G.rsGetAmbientMode.IsValid() && G.rsGetAmbientLight.IsValid()) {
        if (f_bright) {
            if (!g_worldAmbSaved) { g_oldAmbMode = G.rsGetAmbientMode(); g_oldAmb = G.rsGetAmbientLight(); g_worldAmbSaved = true; }
            G.rsSetAmbientMode(3);                            // Flat
            G.rsSetAmbientLight(Color(0.85f, 0.85f, 0.85f, 1));
        } else if (g_worldAmbSaved) {
            G.rsSetAmbientMode(g_oldAmbMode);
            G.rsSetAmbientLight(g_oldAmb);
            g_worldAmbSaved = false;
        }
    }
}

// Called when you change room: forget everything that belonged to the old one.
static void V2Reset() {
    g_jumpBoosted = nullptr;
    g_armActive = -1;
    g_armsTracker = false;
    g_armFieldPlayer = nullptr;
    g_speedoHave = false;
    g_ropeHavePos = false;
    EspReset();
    WorldReset();
    SoundAbort();
}

// ---- callbacks ---------------------------------------------------------------

static void OnRoomChange() {
    // The old room's objects are gone (or about to be): forget them, build again.
    for (int r = 0; r < kRows; r++) { g_rowCube[r].clear(); g_rowLabel[r].clear(); g_rowText[r].clear(); }
    for (Ref *r : {&g_board, &g_bg, &g_outline, &g_title, &g_titleBar, &g_prev, &g_next, &g_notifyText, &g_tipText}) r->clear();
    for (Ref *r : {&g_menu, &g_pointer, &g_notifyGo, &g_leftPlatform, &g_rightPlatform, &g_gunBall, &g_gunLineGo}) Kill(*r);
    PauseTrackers(false);
    g_dancing = false;
    g_gunLine.clear();
    if (g_noclipOn && g_player.get()) {           // leaving the room mid-noclip: put things back
        Buttons none;                             // trigger "released"
        NoclipTick(none);
    }
    g_gravityOff = false;
    ResetMotionState();
    for (Ref *r : {&g_player, &g_rigidbody, &g_leftHand, &g_rightHand, &g_head}) r->clear();
    g_built = false; g_menuShown = false; g_touching = -2; g_haveSpot = false;
}

static void OnUpdate(float dt) {
    g_time += dt;
    g_frames++;
    static double fpsStart = 0;
    if (g_time - fpsStart >= 1.0) { g_fps = (float)(g_frames / (g_time - fpsStart)); g_frames = 0; fpsStart = g_time; }
    if (!Lookups()) return;
    auto *player = (Il2CppObject *)ym::api()->localPlayer();
    if (!player) return;
    if (player != g_player.get()) {                 // first frame in this room
        OnRoomChange();
        g_player.set(player);
        g_leftHand.set(G.leftHand[player]());
        g_rightHand.set(G.rightHand[player]());
        g_head.set(G.headTransform[player]());
        Il2CppObject *pgo = G.compGameObject[player]();
        if (pgo) g_rigidbody.set(G.goGetComponent[pgo](G.tRigidbody));
    }
    if (!g_leftHand.get() || !g_rightHand.get() || !g_head.get()) return;
    if (!g_built) BuildMenu();

    Buttons in = ReadButtons();
    ShowMenu(f_rightHand ? in.b : in.y);
    if (g_menuShown) {
        PlaceMenu();
        CheckPress();
        if (g_dirty) RenderPage();
        UpdateTitle();
        AnimateMenu();
    }
    if (g_notifyUntil > 0 && g_time >= g_notifyUntil) { Show(g_notifyGo, false); g_notifyUntil = 0; }
    FlyTick(in, dt);
    LowGravityTick(in, dt);
    PlatformFor(in.lgrip, g_leftPlatform, g_leftHand);
    PlatformFor(in.rgrip, g_rightPlatform, g_rightHand);
    VelocityFlyTick(in);
    CarTick(in);
    IronManTick(in, dt);
    RopeTick(in, dt);
    SpeedBoostTick(in, dt);
    ApplyGravity();
    NoclipTick(in);
    TeleportGunTick(in);
    try { SpawnOnHandTick(in); ItemQueueTick(); }
    catch (...) { g_itemQueue.clear(); ym::log("prisma: item code error"); }
    ArmsTick();
    DanceTick(in, dt);              // last: it moves your hands after everything else
    SpeedoTick(dt);
    EspTick();
    WorldTick();
    SoundTick();
}

YM_EXPORT int YeepsMod_Init(const YeepsModApi *api) {
    if (!ym::init(api)) return -1;
    ym::onUpdate(OnUpdate);
    ym::log("Prisma Client V2 loaded on %s (%s)", api->gameVersion, api->buildId);
    return 0;
}
