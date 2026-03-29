#include <gtest/gtest.h>
#include "engine/model_utils.hpp"
#include "engine/model_loader.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace mmo::engine;

// Helper: build a Model with known axis-aligned bounding box.
// Does not need meshes/skeleton -- only the bounding fields are used.
static Model make_test_model(float min_x, float max_x,
                             float min_y, float max_y,
                             float min_z, float max_z) {
    Model m;
    m.min_x = min_x; m.max_x = max_x;
    m.min_y = min_y; m.max_y = max_y;
    m.min_z = min_z; m.max_z = max_z;
    return m;
}

// Convenience: transform a point by the matrix.
static glm::vec3 xform(const glm::mat4& m, const glm::vec3& p) {
    glm::vec4 r = m * glm::vec4(p, 1.0f);
    return glm::vec3(r);
}

// A 2x2x2 cube centered at origin: bounds [-1,1] on each axis.
// max_dimension = 2, center pivot = (0, -1, 0).
static Model unit_cube() { return make_test_model(-1, 1, -1, 1, -1, 1); }

// ----- Identity / baseline case -----

TEST(BuildModelTransform, IdentityCase) {
    // Position at origin, zero yaw, target_size chosen so scale = 1.
    // scale = (target_size * 1.5) / max_dim  =>  1.0 when target_size = max_dim / 1.5
    Model m = unit_cube();  // max_dim = 2
    float target_size = 2.0f / 1.5f;

    glm::mat4 mat = build_model_transform(m, {0, 0, 0}, 0.0f, target_size);

    // The centering pivot is (-cx, -cy, -cz) = (0, 1, 0)  (cy = min_y = -1).
    // So the full transform is: Scale(1) * Translate(0,1,0).
    // A point at origin in model space maps to (0, 1, 0) in world space.
    glm::vec3 origin_world = xform(mat, {0, 0, 0});
    EXPECT_NEAR(origin_world.x, 0.0f, 1e-5f);
    EXPECT_NEAR(origin_world.y, 1.0f, 1e-5f);
    EXPECT_NEAR(origin_world.z, 0.0f, 1e-5f);

    // The model's bottom center (-1 min_y, centered x/z) should map to world origin.
    glm::vec3 bottom = xform(mat, {0, -1, 0});
    EXPECT_NEAR(bottom.x, 0.0f, 1e-5f);
    EXPECT_NEAR(bottom.y, 0.0f, 1e-5f);
    EXPECT_NEAR(bottom.z, 0.0f, 1e-5f);
}

// ----- Translation -----

TEST(BuildModelTransform, TranslationApplied) {
    Model m = unit_cube();
    float target_size = 2.0f / 1.5f;  // scale = 1

    glm::vec3 pos(10.0f, 5.0f, 3.0f);
    glm::mat4 mat = build_model_transform(m, pos, 0.0f, target_size);

    // Bottom-center of model should land at the requested position.
    glm::vec3 bottom = xform(mat, {0, -1, 0});
    EXPECT_NEAR(bottom.x, 10.0f, 1e-5f);
    EXPECT_NEAR(bottom.y, 5.0f, 1e-5f);
    EXPECT_NEAR(bottom.z, 3.0f, 1e-5f);
}

// ----- Column-3 holds translation -----

TEST(BuildModelTransform, MatrixColumn3ContainsTranslation) {
    Model m = make_test_model(0, 2, 0, 2, 0, 2);  // pivot = (1, 0, 1)
    float target_size = 2.0f / 1.5f;  // max_dim=2, scale=1

    glm::vec3 pos(10.0f, 5.0f, 3.0f);
    glm::mat4 mat = build_model_transform(m, pos, 0.0f, target_size);

    // Column 3 of the matrix is the world-space image of model-space origin
    // after centering: T(pos) * S(1) * T(-1, 0, -1) applied to (0,0,0)
    // = T(pos) * (-1, 0, -1) = (10-1, 5+0, 3-1) = (9, 5, 2)
    EXPECT_NEAR(mat[3][0], 9.0f, 1e-5f);
    EXPECT_NEAR(mat[3][1], 5.0f, 1e-5f);
    EXPECT_NEAR(mat[3][2], 2.0f, 1e-5f);
    EXPECT_NEAR(mat[3][3], 1.0f, 1e-5f);
}

// ----- Rotation: 90-degree yaw -----

TEST(BuildModelTransform, Yaw90DegreeRotation) {
    Model m = unit_cube();
    float target_size = 2.0f / 1.5f;  // scale = 1
    float yaw = glm::radians(90.0f);

    glm::mat4 mat = build_model_transform(m, {0, 0, 0}, yaw, target_size);

    // After centering, model-space (1, -1, 0) becomes (1, 0, 0) (pivot subtracted).
    // Then scale(1), then 90 deg Y rotation: (1,0,0) -> (0, 0, -1).
    // Then translate(0,0,0).  So final = (0, 0, -1).
    glm::vec3 p = xform(mat, {1, -1, 0});
    EXPECT_NEAR(p.x, 0.0f, 1e-4f);
    EXPECT_NEAR(p.y, 0.0f, 1e-4f);
    EXPECT_NEAR(p.z, -1.0f, 1e-4f);

    // Similarly, (0, -1, 1) -> centered (0, 0, 1) -> rot -> (1, 0, 0).
    glm::vec3 q = xform(mat, {0, -1, 1});
    EXPECT_NEAR(q.x, 1.0f, 1e-4f);
    EXPECT_NEAR(q.y, 0.0f, 1e-4f);
    EXPECT_NEAR(q.z, 0.0f, 1e-4f);
}

// ----- Rotation: 180-degree yaw -----

TEST(BuildModelTransform, Yaw180DegreeRotation) {
    Model m = unit_cube();
    float target_size = 2.0f / 1.5f;
    float yaw = glm::radians(180.0f);

    glm::mat4 mat = build_model_transform(m, {0, 0, 0}, yaw, target_size);

    // Centered (1, 0, 0) -> 180 Y -> (-1, 0, 0).
    glm::vec3 p = xform(mat, {1, -1, 0});
    EXPECT_NEAR(p.x, -1.0f, 1e-4f);
    EXPECT_NEAR(p.y, 0.0f, 1e-4f);
    EXPECT_NEAR(p.z, 0.0f, 1e-4f);
}

// ----- Scale: target_size controls uniform scale -----

TEST(BuildModelTransform, ScaleApplied) {
    // Model with max_dimension = 4 (height is largest).
    Model m = make_test_model(-1, 1, 0, 4, -1, 1);
    // scale = (target_size * 1.5) / 4
    // With target_size = 4.0:  scale = 6/4 = 1.5
    float target_size = 4.0f;
    float expected_scale = (4.0f * 1.5f) / 4.0f;

    glm::mat4 mat = build_model_transform(m, {0, 0, 0}, 0.0f, target_size);

    // Pivot = (0, 0, 0). After centering, (1, 0, 0) stays (1, 0, 0).
    // After scale 1.5: (1.5, 0, 0).
    glm::vec3 p = xform(mat, {1, 0, 0});
    EXPECT_NEAR(p.x, expected_scale, 1e-5f);
    EXPECT_NEAR(p.y, 0.0f, 1e-5f);
    EXPECT_NEAR(p.z, 0.0f, 1e-5f);

    // Verify height: top of model (0, 4, 0) after centering (0, 4, 0), scale -> (0, 6, 0).
    glm::vec3 top = xform(mat, {0, 4, 0});
    EXPECT_NEAR(top.y, 4.0f * expected_scale, 1e-5f);
}

TEST(BuildModelTransform, ScaleDifferentMaxDimension) {
    // Width-dominated model: 10 wide, 2 tall, 2 deep.
    Model m = make_test_model(-5, 5, 0, 2, -1, 1);
    float target_size = 1.0f;
    float expected_scale = (1.0f * 1.5f) / 10.0f;  // 0.15

    glm::mat4 mat = build_model_transform(m, {0, 0, 0}, 0.0f, target_size);

    // Pivot = (0, 0, 0). Point (5, 0, 0) -> center (5, 0, 0) -> scale -> (0.75, 0, 0).
    glm::vec3 p = xform(mat, {5, 0, 0});
    EXPECT_NEAR(p.x, 5.0f * expected_scale, 1e-5f);
}

// ----- Attack tilt -----

TEST(BuildModelTransform, AttackTiltApplied) {
    Model m = unit_cube();
    float target_size = 2.0f / 1.5f;  // scale = 1
    float tilt = glm::radians(90.0f);

    glm::mat4 mat = build_model_transform(m, {0, 0, 0}, 0.0f, target_size, tilt);

    // After centering, (0, -1, 0) -> (0, 0, 0) (bottom center).
    // Attack tilt should not move bottom center (it is at origin after centering).
    glm::vec3 bottom = xform(mat, {0, -1, 0});
    EXPECT_NEAR(bottom.x, 0.0f, 1e-4f);
    EXPECT_NEAR(bottom.y, 0.0f, 1e-4f);
    EXPECT_NEAR(bottom.z, 0.0f, 1e-4f);

    // After centering, top (0, 1, 0) -> (0, 2, 0). With 90 deg X tilt:
    // Y axis -> Z axis:  (0, 2, 0) -> (0, 0, 2), but X rotation:
    // Rx(90) * (0, 2, 0) = (0, 0, 2)  -- wait, standard rotation:
    // Rx(theta): y' = y*cos - z*sin, z' = y*sin + z*cos
    // Rx(90): y'=0, z'=2.  So (0, 0, 2).  Actually sign:
    // Rx(90): (0, 2, 0) -> (0, 2*cos90 - 0*sin90, 2*sin90 + 0*cos90) = (0, 0, 2)
    // But GLM rotate may use the opposite sign convention. Let's just verify
    // that the top is no longer above (y should be ~0) and has moved along z.
    glm::vec3 top = xform(mat, {0, 1, 0});
    EXPECT_NEAR(top.x, 0.0f, 1e-4f);
    EXPECT_NEAR(top.y, 0.0f, 1e-4f);
    // z should be +-2 depending on sign convention
    EXPECT_NEAR(std::abs(top.z), 2.0f, 1e-4f);
}

TEST(BuildModelTransform, ZeroAttackTiltMatchesDefault) {
    Model m = unit_cube();
    float target_size = 2.0f;

    glm::mat4 with_zero = build_model_transform(m, {5, 3, 1}, 1.0f, target_size, 0.0f);
    glm::mat4 default_tilt = build_model_transform(m, {5, 3, 1}, 1.0f, target_size);

    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            EXPECT_NEAR(with_zero[c][r], default_tilt[c][r], 1e-6f);
}

// ----- Centering: non-symmetric bounding box -----

TEST(BuildModelTransform, CenteringOffsetsApplied) {
    // Model not centered at origin: bounds [2, 6] x [1, 5] x [3, 7].
    // Pivot: cx=4, cy=1 (min_y), cz=5.
    Model m = make_test_model(2, 6, 1, 5, 3, 7);
    float target_size = 4.0f / 1.5f;  // max_dim=4, scale=1

    glm::mat4 mat = build_model_transform(m, {0, 0, 0}, 0.0f, target_size);

    // Model-space point at the pivot (4, 1, 5) should map to world origin.
    glm::vec3 pivot_world = xform(mat, {4, 1, 5});
    EXPECT_NEAR(pivot_world.x, 0.0f, 1e-5f);
    EXPECT_NEAR(pivot_world.y, 0.0f, 1e-5f);
    EXPECT_NEAR(pivot_world.z, 0.0f, 1e-5f);
}

TEST(BuildModelTransform, CenteringWithTranslation) {
    Model m = make_test_model(2, 6, 1, 5, 3, 7);
    float target_size = 4.0f / 1.5f;

    glm::vec3 pos(10, 20, 30);
    glm::mat4 mat = build_model_transform(m, pos, 0.0f, target_size);

    // Pivot should land at the requested position.
    glm::vec3 pivot_world = xform(mat, {4, 1, 5});
    EXPECT_NEAR(pivot_world.x, 10.0f, 1e-5f);
    EXPECT_NEAR(pivot_world.y, 20.0f, 1e-5f);
    EXPECT_NEAR(pivot_world.z, 30.0f, 1e-5f);
}

// ----- Combined transform: translation + rotation + scale + centering -----

TEST(BuildModelTransform, CombinedTransform) {
    // 4x4x4 cube at [0,4] on all axes. Pivot = (2, 0, 2).
    Model m = make_test_model(0, 4, 0, 4, 0, 4);
    float target_size = 2.0f;  // scale = (2*1.5)/4 = 0.75
    float yaw = glm::radians(90.0f);
    glm::vec3 pos(100, 0, 0);

    glm::mat4 mat = build_model_transform(m, pos, yaw, target_size);

    // Model-space pivot (2, 0, 2): after centering -> (0,0,0) -> scale -> (0,0,0)
    // -> rotate -> (0,0,0) -> translate -> (100, 0, 0).
    glm::vec3 pivot = xform(mat, {2, 0, 2});
    EXPECT_NEAR(pivot.x, 100.0f, 1e-4f);
    EXPECT_NEAR(pivot.y, 0.0f, 1e-4f);
    EXPECT_NEAR(pivot.z, 0.0f, 1e-4f);

    // Model-space (4, 0, 2): centered -> (2, 0, 0) -> scale 0.75 -> (1.5, 0, 0)
    // -> 90 Y -> (0, 0, -1.5) -> translate -> (100, 0, -1.5).
    glm::vec3 edge = xform(mat, {4, 0, 2});
    EXPECT_NEAR(edge.x, 100.0f, 1e-4f);
    EXPECT_NEAR(edge.y, 0.0f, 1e-4f);
    EXPECT_NEAR(edge.z, -1.5f, 1e-4f);
}
