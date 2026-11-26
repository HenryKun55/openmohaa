#ifndef VITA_AIM_ASSIST_H
#define VITA_AIM_ASSIST_H

#include <stdint.h>
#include <string.h>
#include <math.h>

#define VITA_AIM_MAX_FRICTION 0.65f
#define VITA_AIM_MAX_RADIUS 12.0f
#define VITA_AIM_MAX_MAGNETISM 0.35f
#define VITA_AIM_MAX_ROTATION_CAP 0.32f
#define VITA_AIM_MAX_ROTATION_RATE 19.2f
#define VITA_AIM_LEGACY_FRICTION_CAP 0.45f

typedef struct {
    float perCommand;
    float perSecond;
} vitaAimRotationLimits_t;

typedef struct {
    float friction, radius, magnetism;
    vitaAimRotationLimits_t rotation;
} vitaAimPreset_t;

enum { VITA_AIM_BALANCED, VITA_AIM_STRONG, VITA_AIM_VERY_STRONG, VITA_AIM_PRESET_COUNT };
static const vitaAimPreset_t vitaAimPresets[VITA_AIM_PRESET_COUNT] = {
    {0.40f, 8.0f, 0.15f, {0.16f, 9.6f}},
    {0.55f, 10.0f, 0.25f, {0.25f, 15.0f}},
    {0.65f, 12.0f, 0.35f, {0.32f, 19.2f}}
};

static inline int VitaAimPresetIndex(vitaAimPreset_t tuning)
{
    for (int i = 0; i < VITA_AIM_PRESET_COUNT; ++i) {
        const vitaAimPreset_t *preset = &vitaAimPresets[i];
        if (fabsf(tuning.friction - preset->friction) < 0.0001f &&
            fabsf(tuning.radius - preset->radius) < 0.0001f &&
            fabsf(tuning.magnetism - preset->magnetism) < 0.0001f &&
            fabsf(tuning.rotation.perCommand - preset->rotation.perCommand) < 0.0001f &&
            fabsf(tuning.rotation.perSecond - preset->rotation.perSecond) < 0.0001f) {
            return i;
        }
    }
    return -1;
}

typedef enum {
    VITA_AIM_NO_SERVER, VITA_AIM_NO_GAME_EXPORT, VITA_AIM_BAD_SLOT, VITA_AIM_BAD_MODE,
    VITA_AIM_BAD_PLAYER, VITA_AIM_BAD_STATE, VITA_AIM_BAD_TEAM, VITA_AIM_SCANNED
} vitaAimDebugStage_t;

typedef enum {
    VITA_MAG_NO_TARGET, VITA_MAG_BLOCKED, VITA_MAG_OFF, VITA_MAG_NO_INPUT,
    VITA_MAG_CENTERED, VITA_MAG_AXIS_AWAY, VITA_MAG_SWIPE_AWAY, VITA_MAG_TANGENT,
    VITA_MAG_NO_GAIN, VITA_MAG_NO_ROOM, VITA_MAG_CAPPED, VITA_MAG_APPLIED
} vitaAimMagReason_t;

typedef enum {
    VITA_ROT_NO_TARGET, VITA_ROT_BLOCKED, VITA_ROT_OFF, VITA_ROT_NO_INPUT,
    VITA_ROT_NO_TIME, VITA_ROT_NO_STRAFE, VITA_ROT_WEAK_STRAFE, VITA_ROT_NO_VELOCITY,
    VITA_ROT_VELOCITY_OPPOSED, VITA_ROT_CENTERED, VITA_ROT_WRONG_PARALLAX,
    VITA_ROT_LOOK_AWAY, VITA_ROT_OUTER_CONE, VITA_ROT_NO_ROOM, VITA_ROT_APPLIED
} vitaAimRotReason_t;

typedef struct {
    vitaAimMagReason_t reason;
    float error, raw, applied;
} vitaAimMagDebug_t;

typedef struct {
    vitaAimDebugStage_t stage;
    int playerTeam, enemyTeam;
    int enemies, eligible, inCone, checked, visible;
    int snapshotRejected, pvsRejected, solidRejected, smokeRejected;
    int lastChecked, nearest;
    float nearestAngle, selectedAngle;
    float frictionMultiplier, rotationalYaw, strafe, strafeYawRate;
    float approach;
    vitaAimMagDebug_t magYaw, magPitch;
    vitaAimRotReason_t rotReason;
} vitaAimDebug_t;

typedef struct vitaAimTarget_s {
    float weight;
    float right;
    float down;
    int entityNum;
    float strafeYawRate;
    vitaAimDebug_t *debug;
} vitaAimTarget_t;

typedef struct {
    float yaw;
    float pitch;
} vitaAimLook_t;

static inline float VitaAimSetting(float value, float maximum, float fallback)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    // Vita builds use fast math, so check non-finite settings by representation.
    if ((bits & 0x7fffffffU) > 0x7f800000U) {
        return fallback;
    }
    if (bits & 0x80000000U) {
        return 0.0f;
    }
    return bits >= 0x7f800000U || value > maximum ? maximum : value;
}

// Older configurations store a slider in [0, 1] which represented up to 10% gain.
static inline float VitaAimMagnetismGain(float gain, float legacySetting)
{
    return gain < 0.0f ? 0.10f * VitaAimSetting(legacySetting, 1.0f, 0.0f)
                       : VitaAimSetting(gain, VITA_AIM_MAX_MAGNETISM, 0.0f);
}

static inline vitaAimTarget_t VitaAimProject(float forward, float right, float up, float radiusTangent)
{
    vitaAimTarget_t target = {0.0f, 0.0f, 0.0f, -1, 0.0f, NULL};
    const float cone = forward * radiusTangent;
    const float offsetSquared = right * right + up * up;
    if (cone > 0.0f && offsetSquared < cone * cone) {
        const float inverseCone = 1.0f / cone;
        const float offset = offsetSquared * inverseCone * inverseCone;
        const float edge = 1.0f - offset;
        target.weight = edge * edge * (1.0f + 2.0f * offset);
        target.right = right * inverseCone;
        target.down = -up * inverseCone;
    }
    return target;
}

static inline int VitaAimActorActive(int present, int dead, float health)
{
    return present && !dead && health > 0.0f;
}

// Keep only the two strongest candidates. Ties prefer the nearer actor.
#define VITA_AIM_TRACE_BUDGET 2
typedef struct {
    vitaAimTarget_t target;
    float distanceSquared;
    float point[3];
    float score;
} vitaAimCandidate_t;

static inline void VitaAimRank(vitaAimCandidate_t best[VITA_AIM_TRACE_BUDGET], vitaAimCandidate_t candidate,
                              int preferredEntity)
{
    if (candidate.target.weight <= 0.0f) {
        return;
    }
    // A challenger needs a meaningful positional advantage. Visibility is checked again every frame.
    candidate.score = candidate.target.weight + (candidate.target.entityNum == preferredEntity ? 0.08f : 0.0f);
    for (int i = 0; i < VITA_AIM_TRACE_BUDGET; ++i) {
        if (candidate.score > best[i].score ||
            (candidate.score == best[i].score && candidate.distanceSquared < best[i].distanceSquared)) {
            for (int j = VITA_AIM_TRACE_BUDGET - 1; j > i; --j) {
                best[j] = best[j - 1];
            }
            best[i] = candidate;
            return;
        }
    }
}

typedef int (*vitaAimVisible_t)(const vitaAimCandidate_t *candidate, void *context);

static inline vitaAimTarget_t VitaAimVisibleTarget(const vitaAimCandidate_t best[VITA_AIM_TRACE_BUDGET],
                                                   vitaAimVisible_t visible, void *context)
{
    const vitaAimTarget_t none = {0.0f, 0.0f, 0.0f, -1, 0.0f, NULL};
    for (int i = 0; i < VITA_AIM_TRACE_BUDGET && best[i].target.weight > 0.0f; ++i) {
        if (visible(&best[i], context)) {
            return best[i].target;
        }
    }
    return none;
}

// Attribute the filtered mouse delta to fresh touch without borrowing motion from an opposite input.
static inline float VitaAimTouchShare(float touch, float mouse)
{
    return touch * mouse > 0.0f ? fminf(1.0f, touch / mouse) : 0.0f;
}

static inline float VitaAimEngagement(float previous, int elapsed)
{
    const float step = elapsed > 0 ? fminf(24.0f, (float)elapsed) / 72.0f : 0.0f;
    return fminf(1.0f, previous + step);
}

static inline float VitaAimAxisMeasured(float delta, float error, float strength, float friction,
                                       float magnetism, float approach, vitaAimMagDebug_t *debug)
{
    const float slowed = delta * (1.0f - strength * friction);
    const float limit = fabsf(delta) * (strength * magnetism);
    if (debug) {
        debug->error = error;
        debug->raw = copysignf(limit, delta);
        debug->applied = 0.0f;
    }
    if (delta * error <= 0.0f || approach <= 0.0f || limit <= 0.0f) {
        if (debug) {
            if (magnetism <= 0.0f) debug->reason = VITA_MAG_OFF;
            else if (delta == 0.0f) debug->reason = VITA_MAG_NO_INPUT;
            else if (error == 0.0f) debug->reason = VITA_MAG_CENTERED;
            else if (delta * error < 0.0f) debug->reason = VITA_MAG_AXIS_AWAY;
            else if (approach < 0.0f) debug->reason = VITA_MAG_SWIPE_AWAY;
            else if (approach == 0.0f) debug->reason = VITA_MAG_TANGENT;
            else debug->reason = VITA_MAG_NO_GAIN;
        }
        return slowed;
    }
    const float room = fmaxf(0.0f, fabsf(error) - fabsf(slowed));
    const float bias = fminf(room, limit);
    const float correction = copysignf(bias, delta);
    if (debug) {
        debug->reason = room <= 0.0f ? VITA_MAG_NO_ROOM : room < limit ? VITA_MAG_CAPPED : VITA_MAG_APPLIED;
        debug->applied = correction;
    }
    return slowed + correction;
}

static inline float VitaAimAxis(float delta, float error, float strength, float friction, float magnetism)
{
    return VitaAimAxisMeasured(delta, error, strength, friction, magnetism, delta * error, NULL);
}

static inline float VitaAimRotation(vitaAimLook_t look, float yaw, float error, float strafeYawRate,
                                   float strafe, float strength, int elapsed, vitaAimRotationLimits_t limits,
                                   vitaAimRotReason_t *reason)
{
    // View-relative strafe velocity predicts parallax. Require active look and command strafing
    // in the same direction as the actual movement to avoid drift or pulls in passing.
    if (elapsed <= 0 || fabsf(strafe) <= 0.2f || strafe * strafeYawRate <= 0.0f ||
        strafeYawRate * error <= 0.0f || look.yaw * error < 0.0f) {
        if (reason) {
            if (elapsed <= 0) *reason = VITA_ROT_NO_TIME;
            else if (strafe == 0.0f) *reason = VITA_ROT_NO_STRAFE;
            else if (fabsf(strafe) <= 0.2f) *reason = VITA_ROT_WEAK_STRAFE;
            else if (strafeYawRate == 0.0f) *reason = VITA_ROT_NO_VELOCITY;
            else if (strafe * strafeYawRate < 0.0f) *reason = VITA_ROT_VELOCITY_OPPOSED;
            else if (error == 0.0f) *reason = VITA_ROT_CENTERED;
            else if (look.yaw * error < 0.0f) *reason = VITA_ROT_LOOK_AWAY;
            else *reason = VITA_ROT_WRONG_PARALLAX;
        }
        return 0.0f;
    }
    const float room = fmaxf(0.0f, fabsf(error) - fabsf(yaw));
    const float intent = fabsf(look.yaw) + fabsf(look.pitch);
    const float seconds = fminf((float)elapsed, 24.0f) * 0.001f;
    const float rate = fminf(limits.perSecond, fabsf(strafeYawRate) * 0.30f);
    const float cap = fminf(limits.perCommand, fminf(room, intent * 0.30f));
    const float innerCone = fmaxf(0.0f, 2.0f * strength - 1.0f);
    const float strafeWeight = fminf(1.0f, (fabsf(strafe) - 0.2f) / 0.6f);
    const float correction = fminf(cap, rate * seconds * innerCone * strafeWeight);
    if (reason) {
        *reason = innerCone == 0.0f ? VITA_ROT_OUTER_CONE : correction == 0.0f ? VITA_ROT_NO_ROOM : VITA_ROT_APPLIED;
    }
    return copysignf(correction, error);
}

static inline vitaAimLook_t VitaAimCorrection(int enabled, vitaAimTarget_t target,
                                              vitaAimLook_t touch, vitaAimLook_t stick,
                                              float radius, float engagement, float friction, float magnetism,
                                              int rotational, float strafe, int elapsed, vitaAimRotationLimits_t limits)
{
    vitaAimLook_t correction = {0.0f, 0.0f};
    const vitaAimLook_t look = {touch.yaw + stick.yaw, touch.pitch + stick.pitch};
    vitaAimDebug_t *debug = target.debug;
    const float yawError = -target.right * radius;
    const float pitchError = target.down * radius;
    const float approach = look.yaw * yawError + look.pitch * pitchError;
    if (debug) {
        const vitaAimMagDebug_t yawSample = {VITA_MAG_NO_TARGET, yawError, 0.0f, 0.0f};
        const vitaAimMagDebug_t pitchSample = {VITA_MAG_NO_TARGET, pitchError, 0.0f, 0.0f};
        debug->magYaw = yawSample;
        debug->magPitch = pitchSample;
        debug->approach = approach;
        debug->strafe = strafe;
        debug->strafeYawRate = target.strafeYawRate;
        debug->frictionMultiplier = 1.0f;
        debug->rotationalYaw = 0.0f;
    }
    if (!enabled || target.weight <= 0.0f || (look.yaw == 0.0f && look.pitch == 0.0f)) {
        if (debug) {
            debug->magYaw.reason = debug->magPitch.reason = target.weight <= 0.0f ? VITA_MAG_NO_TARGET :
                !enabled ? VITA_MAG_BLOCKED : VITA_MAG_NO_INPUT;
            debug->rotReason = target.weight <= 0.0f ? VITA_ROT_NO_TARGET :
                !enabled ? VITA_ROT_BLOCKED : rotational ? VITA_ROT_NO_INPUT : VITA_ROT_OFF;
        }
        return correction;
    }
    const float strength = target.weight * engagement;
    // Strong presets retain the old maximum slowdown on a deliberate retreat.
    if (approach < 0.0f) {
        friction = fminf(friction, VITA_AIM_LEGACY_FRICTION_CAP);
    }
    if (debug) {
        debug->frictionMultiplier = 1.0f - strength * friction;
        debug->rotReason = rotational ? VITA_ROT_LOOK_AWAY : VITA_ROT_OFF;
    }
    const float yaw = VitaAimAxisMeasured(look.yaw, yawError, strength, friction, magnetism, approach,
                                         debug ? &debug->magYaw : NULL);
    correction.yaw = yaw - look.yaw;
    correction.pitch = VitaAimAxisMeasured(look.pitch, pitchError, strength, friction, magnetism, approach,
                                           debug ? &debug->magPitch : NULL) - look.pitch;
    if (rotational && look.pitch * pitchError >= 0.0f) {
        const float rotation = VitaAimRotation(look, yaw, yawError, target.strafeYawRate, strafe, strength, elapsed, limits,
                                               debug ? &debug->rotReason : NULL);
        if (debug) {
            debug->rotationalYaw = rotation;
        }
        correction.yaw += rotation;
    }
    return correction;
}

#endif
