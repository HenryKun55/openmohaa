// Host test: cc -std=c17 -O2 -Wall -Wextra tests/test_vita_aim_assist.c -lm -o /tmp/test_vita_aim_assist
#include "../code/qcommon/vita_aim_assist.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void check(int passed, const char *expression, int line)
{
    if (!passed) {
        fprintf(stderr, "line %d: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

static const float radius = 0.08748866f; // tan(5 degrees)

static void test_cone(void)
{
    const float edge = 100.0f * radius;
    const vitaAimTarget_t center = VitaAimProject(100.0f, 0.0f, 0.0f, radius);
    const vitaAimTarget_t middle = VitaAimProject(100.0f, edge * 0.5f, 0.0f, radius);
    const vitaAimTarget_t entering = VitaAimProject(100.0f, edge * 0.99f, 0.0f, radius);
    const vitaAimTarget_t outside = VitaAimProject(100.0f, edge * 1.01f, 0.0f, radius);
    CHECK(center.weight == 1.0f);
    CHECK(middle.weight < center.weight && middle.weight > 0.80f);
    CHECK(entering.weight > 0.0f && entering.weight < 0.002f);
    CHECK(outside.weight == 0.0f);
    CHECK(VitaAimProject(-100.0f, 0.0f, 0.0f, radius).weight == 0.0f);
    CHECK(VitaAimProject(100.0f, 0.0f, edge * 0.5f, radius).down < 0.0f);
}

typedef struct {
    unsigned visibleMask;
    int queries;
} sight_t;

static int mock_visible(const vitaAimCandidate_t *candidate, void *context)
{
    sight_t *sight = context;
    ++sight->queries;
    return sight->visibleMask & (1U << candidate->target.entityNum);
}

static void rank_actor(vitaAimCandidate_t *best, int entityNum, float side, float forward,
                       int present, int dead, float health, int preferred)
{
    if (VitaAimActorActive(present, dead, health)) {
        vitaAimCandidate_t candidate = {
            .target = VitaAimProject(forward, side, 0.0f, radius),
            .distanceSquared = forward * forward + side * side,
        };
        candidate.target.entityNum = entityNum;
        VitaAimRank(best, candidate, preferred);
    }
}

static void test_selection(void)
{
    vitaAimCandidate_t best[VITA_AIM_TRACE_BUDGET] = {0};
    sight_t sight = {.visibleMask = 1U << 2};
    rank_actor(best, 3, 4.0f, 100.0f, 1, 0, 100.0f, -1);
    rank_actor(best, 2, 2.0f, 100.0f, 1, 0, 100.0f, -1);
    rank_actor(best, 1, -1.0f, 100.0f, 1, 0, 100.0f, -1);
    CHECK(best[0].target.entityNum == 1 && best[1].target.entityNum == 2);
    CHECK(VitaAimVisibleTarget(best, mock_visible, &sight).right > 0.0f);
    CHECK(sight.queries == 2);

    sight = (sight_t){.visibleMask = (1U << 1) | (1U << 2)};
    CHECK(VitaAimVisibleTarget(best, mock_visible, &sight).right < 0.0f);
    CHECK(sight.queries == 1);

    sight = (sight_t){.visibleMask = 1U << 3};
    CHECK(VitaAimVisibleTarget(best, mock_visible, &sight).weight == 0.0f);
    CHECK(sight.queries == VITA_AIM_TRACE_BUDGET);

    memset(best, 0, sizeof(best));
    rank_actor(best, 1, 0.0f, 100.0f, 1, 1, 100.0f, -1);
    rank_actor(best, 2, 0.0f, 100.0f, 0, 0, 100.0f, -1);
    rank_actor(best, 3, 0.0f, 100.0f, 1, 0, 0.0f, -1);
    sight = (sight_t){.visibleMask = ~0U};
    CHECK(VitaAimVisibleTarget(best, mock_visible, &sight).weight == 0.0f);
    CHECK(sight.queries == 0);

    rank_actor(best, 1, 0.0f, 200.0f, 1, 0, 100.0f, -1);
    rank_actor(best, 2, 0.0f, 100.0f, 1, 0, 100.0f, -1);
    CHECK(best[0].target.entityNum == 2 && best[1].target.entityNum == 1);

    memset(best, 0, sizeof(best));
    rank_actor(best, 1, -2.5f, 100.0f, 1, 0, 100.0f, 1);
    rank_actor(best, 2, 2.0f, 100.0f, 1, 0, 100.0f, 1);
    CHECK(best[0].target.entityNum == 1);
    sight = (sight_t){.visibleMask = 1U << 2};
    CHECK(VitaAimVisibleTarget(best, mock_visible, &sight).entityNum == 2);
    memset(best, 0, sizeof(best));
    rank_actor(best, 1, -5.5f, 100.0f, 1, 0, 100.0f, 1);
    rank_actor(best, 2, 2.0f, 100.0f, 1, 0, 100.0f, 1);
    CHECK(best[0].target.entityNum == 2);
}

static void near(float a, float b)
{
    CHECK(fabsf(a - b) < 0.00001f);
}

static const vitaAimRotationLimits_t legacyRotation = {0.12f, 6.0f};
static const vitaAimLook_t idle = {0.0f, 0.0f};
static const vitaAimTarget_t center = {.weight = 1.0f};

static void test_look_input(void)
{
    const vitaAimLook_t touch = {0.5f, -0.5f};
    const vitaAimLook_t stick = {1.0f, -1.0f};
    const vitaAimLook_t cancelled = {-0.5f, 0.5f};
    const vitaAimLook_t a = VitaAimCorrection(1, center, touch, idle, 7.0f, 1.0f, 0.35f, 0, 0, 0, 16, legacyRotation);
    const vitaAimLook_t b = VitaAimCorrection(1, center, idle, stick, 7.0f, 1.0f, 0.35f, 0, 0, 0, 16, legacyRotation);
    const vitaAimLook_t both = VitaAimCorrection(1, center, touch, stick, 7.0f, 1.0f, 0.35f, 0, 0, 0, 16, legacyRotation);
    near(a.yaw, -0.175f);
    near(b.yaw, -0.35f);
    near(both.yaw, a.yaw + b.yaw);
    near(both.pitch, a.pitch + b.pitch);
    CHECK(VitaAimCorrection(1, center, touch, cancelled, 7, 1, 0.45f, 0.10f, 1, 1, 16, legacyRotation).yaw == 0.0f);
    CHECK(VitaAimCorrection(0, center, touch, stick, 7, 1, 0.45f, 0.10f, 1, 1, 16, legacyRotation).yaw == 0.0f);
    CHECK(VitaAimCorrection(0, center, touch, stick, 7, 1, 0.45f, 0.10f, 1, 1, 16, legacyRotation).pitch == 0.0f);
    CHECK(VitaAimTouchShare(0, 2) == 0 && VitaAimTouchShare(-1, 2) == 0);
    near(VitaAimTouchShare(1, 2), 0.5f);
    near(VitaAimTouchShare(4, 2), 1.0f);
    near(VitaAimEngagement(0, 18), 0.25f);
    near(VitaAimEngagement(0.9f, 16), 1.0f);
    // Corrections are proportional to the existing post-ADS deltas.
    near(VitaAimAxis(touch.yaw * 0.8f, 0, 1, 0.35f, 0), touch.yaw * 0.8f * 0.65f);
    near(VitaAimAxis(stick.yaw * 0.5f, 0, 1, 0.35f, 0), stick.yaw * 0.5f * 0.65f);
}

static void test_magnetism_and_rotation(void)
{
    const vitaAimTarget_t left = {.weight = 0.95f, .right = -0.3f, .strafeYawRate = 120.0f};
    const vitaAimLook_t toward = {0.5f, 0.0f}, away = {-0.5f, 0.0f}, pitch = {0.0f, 0.5f};
    const vitaAimTarget_t lost = {0};
    near(VitaAimAxis(0.5f, 2.0f, 1.0f, 0.0f, 0.10f), 0.55f);
    near(VitaAimAxis(0.5f, 2.0f, 1.0f, 0.0f, 0.05f), 0.525f);
    near(VitaAimAxis(0.5f, 0.51f, 1.0f, 0.0f, 0.10f), 0.51f);
    near(VitaAimAxis(-0.5f, 2.0f, 1.0f, 0.0f, 0.10f), -0.5f);
    near(VitaAimAxis(0.0f, 2.0f, 1.0f, 0.0f, 0.10f), 0.0f);

    const float rotating = VitaAimCorrection(1, left, pitch, idle, 7, 1, 0, 0, 1, 1, 16, legacyRotation).yaw;
    CHECK(rotating > 0.0f && rotating <= 0.096f);
    const float lightStrafe = VitaAimCorrection(1, left, pitch, idle, 7, 1, 0, 0, 1, 0.3f, 16, legacyRotation).yaw;
    CHECK(lightStrafe > 0.0f && lightStrafe < rotating);
    near(VitaAimCorrection(1, left, toward, idle, 7, 1, 0, 0, 1, 1, 500, legacyRotation).yaw, 0.12f);
    CHECK(VitaAimCorrection(1, left, away, idle, 7, 1, 0, 0.10f, 1, 1, 16, legacyRotation).yaw == 0.0f);
    CHECK(VitaAimCorrection(1, left, pitch, idle, 7, 1, 0, 0, 1, -1, 16, legacyRotation).yaw == 0.0f);
    CHECK(VitaAimCorrection(1, left, pitch, idle, 7, 1, 0, 0, 1, 0, 16, legacyRotation).yaw == 0.0f);
    CHECK(VitaAimCorrection(1, left, idle, idle, 7, 1, 0, 0.10f, 1, 1, 16, legacyRotation).yaw == 0.0f);
    CHECK(VitaAimCorrection(1, left, pitch, idle, 7, 1, 0, 0, 0, 1, 16, legacyRotation).yaw == 0.0f);
    CHECK(VitaAimCorrection(1, lost, pitch, idle, 7, 1, 0.45f, 0.10f, 1, 1, 16, legacyRotation).yaw == 0.0f);
    CHECK(VitaAimCorrection(1, lost, pitch, idle, 7, 1, 0.45f, 0.10f, 1, 1, 16, legacyRotation).pitch == 0.0f);
}

static void test_diagnostics_preserve_correction(void)
{
    vitaAimTarget_t target = {.weight = 0.95f, .right = -0.3f, .down = 0.2f, .strafeYawRate = 120.0f};
    const vitaAimLook_t touch = {0.2f, 0.1f}, stick = {0.3f, 0.2f};
    const vitaAimLook_t expected = VitaAimCorrection(1, target, touch, stick, 7, 1, 0.35f, 0.10f, 1, 1, 16, legacyRotation);
    vitaAimDebug_t debug = {0};
    target.debug = &debug;
    const vitaAimLook_t actual = VitaAimCorrection(1, target, touch, stick, 7, 1, 0.35f, 0.10f, 1, 1, 16, legacyRotation);
    CHECK(actual.yaw == expected.yaw && actual.pitch == expected.pitch);
    near(debug.frictionMultiplier, 1.0f - 0.95f * 0.35f);
    near(debug.magYaw.applied, 0.5f * 0.10f * 0.95f);
    near(debug.magPitch.applied, 0.3f * 0.10f * 0.95f);
    CHECK(debug.rotationalYaw > 0.0f);
    near(actual.yaw, 0.5f * (debug.frictionMultiplier - 1.0f) + debug.magYaw.applied + debug.rotationalYaw);
}

static void test_direction_and_hardware_scale(void)
{
    vitaAimDebug_t debug = {0};
    vitaAimTarget_t target = {.weight = 0.94f, .right = 0.8f / 7.0f, .debug = &debug};
    const vitaAimLook_t rightSwipe = {-0.189f, 0.0f}, leftSwipe = {0.189f, 0.0f};
    const vitaAimLook_t toward = VitaAimCorrection(1, target, rightSwipe, idle, 7, 1, 0.45f, 0.10f, 1, 0, 16, legacyRotation);
    near(debug.magYaw.error, -0.8f);
    near(debug.frictionMultiplier, 0.577f);
    near(debug.magYaw.raw, -0.017766f);
    CHECK(debug.magYaw.reason == VITA_MAG_APPLIED && debug.approach > 0.0f);
    CHECK(debug.magPitch.reason == VITA_MAG_NO_INPUT);
    CHECK(debug.rotReason == VITA_ROT_NO_STRAFE);
    near(rightSwipe.yaw + toward.yaw, -0.126819f);

    const vitaAimLook_t stickOnly = VitaAimCorrection(1, target, idle, rightSwipe, 7, 1, 0.45f, 0.10f, 1, 0, 16, legacyRotation);
    CHECK(stickOnly.yaw == toward.yaw && debug.magYaw.reason == VITA_MAG_APPLIED);
    const vitaAimLook_t away = VitaAimCorrection(1, target, leftSwipe, idle, 7, 1, 0.45f, 0.10f, 1, 0, 16, legacyRotation);
    near(leftSwipe.yaw + away.yaw, 0.109053f);
    CHECK(debug.magYaw.reason == VITA_MAG_AXIS_AWAY && debug.magYaw.applied == 0.0f);

    // A diagonal swipe can approach on yaw while retreating overall.
    target.down = 0.2f;
    const vitaAimLook_t diagonal = {-0.189f, -0.5f};
    const vitaAimLook_t retreat = VitaAimCorrection(1, target, diagonal, idle, 7, 1, 0.45f, 0.10f, 1, 0, 16, legacyRotation);
    CHECK(debug.approach < 0.0f && debug.magYaw.reason == VITA_MAG_SWIPE_AWAY);
    CHECK(debug.magYaw.raw < 0.0f && debug.magYaw.applied == 0.0f);
    near(diagonal.yaw + retreat.yaw, -0.109053f);

    vitaAimMagDebug_t axis = {0};
    near(VitaAimAxisMeasured(0.5f, 0.51f, 1, 0, 0.10f, 1, &axis), 0.51f);
    CHECK(axis.reason == VITA_MAG_CAPPED);
    near(axis.raw, 0.05f);
    near(axis.applied, 0.01f);
    near(VitaAimAxisMeasured(0.5f, 0.1f, 1, 0, 0.10f, 1, &axis), 0.5f);
    CHECK(axis.reason == VITA_MAG_NO_ROOM && axis.applied == 0.0f);
    near(VitaAimAxisMeasured(0.5f, 0.51f, 1, 0, 0.10f, 0, &axis), 0.5f);
    CHECK(axis.reason == VITA_MAG_TANGENT && axis.applied == 0.0f);
}

static void test_rotation_reasons(void)
{
    vitaAimDebug_t debug = {0};
    vitaAimTarget_t target = {.weight = 0.95f, .right = -0.3f, .strafeYawRate = 120.0f, .debug = &debug};
    const vitaAimLook_t pitch = {0.0f, 0.5f};
    VitaAimCorrection(1, target, pitch, idle, 7, 1, 0, 0, 0, 1, 16, legacyRotation);
    CHECK(debug.rotReason == VITA_ROT_OFF);
    VitaAimCorrection(1, target, pitch, idle, 7, 1, 0, 0, 1, 0, 16, legacyRotation);
    CHECK(debug.rotReason == VITA_ROT_NO_STRAFE);
    VitaAimCorrection(1, target, pitch, idle, 7, 1, 0, 0, 1, 0.1f, 16, legacyRotation);
    CHECK(debug.rotReason == VITA_ROT_WEAK_STRAFE);
    VitaAimCorrection(1, target, pitch, idle, 7, 1, 0, 0, 1, 0.2f, 16, legacyRotation);
    CHECK(debug.rotReason == VITA_ROT_WEAK_STRAFE);
    VitaAimCorrection(1, target, pitch, idle, 7, 1, 0, 0, 1, -1, 16, legacyRotation);
    CHECK(debug.rotReason == VITA_ROT_VELOCITY_OPPOSED);
    VitaAimCorrection(1, target, pitch, idle, 7, 0.4f, 0, 0, 1, 1, 16, legacyRotation);
    CHECK(debug.rotReason == VITA_ROT_OUTER_CONE);
    VitaAimCorrection(1, target, pitch, idle, 7, 1, 0, 0, 1, 1, 16, legacyRotation);
    CHECK(debug.rotReason == VITA_ROT_APPLIED);
    target.weight = 0;
    VitaAimCorrection(1, target, pitch, idle, 7, 1, 0, 0, 1, 1, 16, legacyRotation);
    CHECK(debug.rotReason == VITA_ROT_NO_TARGET && debug.rotationalYaw == 0.0f);
}

static float rotation_over_second(vitaAimRotationLimits_t limits, int commands)
{
    const vitaAimLook_t input = {1.1f, 0.0f};
    float total = 0.0f;
    for (int i = 0; i < commands; ++i) {
        const int elapsed = (i + 1) * 1000 / commands - i * 1000 / commands;
        const float correction = VitaAimRotation(input, 0, 10, 1000, 1, 1, elapsed, limits, NULL);
        CHECK(correction <= limits.perCommand + 0.00001f);
        CHECK(correction <= limits.perSecond * elapsed / 1000.0f + 0.00001f);
        total += correction;
    }
    return total;
}

static void test_presets(void)
{
    const float friction[] = {0.40f, 0.55f, 0.65f};
    const float radiusDegrees[] = {8.0f, 10.0f, 12.0f};
    const float magnetism[] = {0.15f, 0.25f, 0.35f};
    const float cap[] = {0.16f, 0.25f, 0.32f};
    const float rate[] = {9.6f, 15.0f, 19.2f};
    const vitaAimLook_t toward = {0.5f, 0.0f}, away = {-0.5f, 0.0f};
    const vitaAimTarget_t left = {.weight = 1.0f, .right = -0.4f, .strafeYawRate = 120.0f};
    for (int i = 0; i < VITA_AIM_PRESET_COUNT; ++i) {
        const vitaAimPreset_t preset = vitaAimPresets[i];
        CHECK(VitaAimPresetIndex(preset) == i);
        near(preset.friction, friction[i]);
        near(preset.radius, radiusDegrees[i]);
        near(preset.magnetism, magnetism[i]);
        near(preset.rotation.perCommand, cap[i]);
        near(preset.rotation.perSecond, rate[i]);
        const vitaAimLook_t approach = VitaAimCorrection(1, left, toward, idle, preset.radius, 1,
            preset.friction, preset.magnetism, 0, 0, 16, preset.rotation);
        near(toward.yaw + approach.yaw, toward.yaw * (1.0f - friction[i] + magnetism[i]));
        const vitaAimLook_t retreat = VitaAimCorrection(1, left, away, idle, preset.radius, 1,
            preset.friction, preset.magnetism, 1, 1, 16, preset.rotation);
        near(away.yaw + retreat.yaw, away.yaw * (1.0f - fminf(friction[i], 0.45f)));
        CHECK(VitaAimCorrection(0, left, toward, idle, preset.radius, 1,
            preset.friction, preset.magnetism, 1, 1, 16, preset.rotation).yaw == 0.0f);
        CHECK(VitaAimCorrection(1, left, idle, idle, preset.radius, 1,
            preset.friction, preset.magnetism, 1, 1, 16, preset.rotation).yaw == 0.0f);
        CHECK(fabsf(rotation_over_second(preset.rotation, 120) - rate[i]) < 0.0001f);
        CHECK(fabsf(rotation_over_second(preset.rotation, 60) - rate[i]) < rate[i] * 0.02f);
        near(rotation_over_second(preset.rotation, 30), cap[i] * 30);
        near(VitaAimRotation((vitaAimLook_t){1.1f, 0}, 0, 10, 1000, 1, 1, 500, preset.rotation, NULL), cap[i]);
    }
    vitaAimPreset_t custom = vitaAimPresets[VITA_AIM_STRONG];
    custom.rotation.perSecond = 7.4f;
    CHECK(VitaAimPresetIndex(custom) == -1);
    near(VitaAimMagnetismGain(-1.0f, 0.75f), 0.075f);
    near(VitaAimMagnetismGain(0.25f, 0.75f), 0.25f);
    near(VitaAimMagnetismGain(1.0f, 0.75f), 0.35f);
}

int main(void)
{
    test_cone();
    test_selection();
    test_look_input();
    test_magnetism_and_rotation();
    test_diagnostics_preserve_correction();
    test_direction_and_hardware_scale();
    test_rotation_reasons();
    test_presets();
    CHECK(VitaAimSetting(-2.0f, VITA_AIM_MAX_FRICTION, 0.35f) == 0.0f);
    CHECK(VitaAimSetting(1.0f, VITA_AIM_MAX_FRICTION, 0.35f) == 0.65f);
    CHECK(VitaAimSetting(15.0f, 8.0f, 5.0f) == 8.0f);
    CHECK(VitaAimSetting(INFINITY, 8.0f, 5.0f) == 8.0f);
    CHECK(VitaAimSetting(NAN, 8.0f, 5.0f) == 5.0f);
    puts("Vita aim assist tests passed");
    return 0;
}
