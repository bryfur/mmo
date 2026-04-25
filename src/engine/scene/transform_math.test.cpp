#include "engine/scene/transform_math.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

using namespace mmo::engine::scene;

TEST(TransformMath, IdentityIsUniformScale) {
    EXPECT_TRUE(has_uniform_scale(glm::mat4(1.0f)));
}

TEST(TransformMath, UniformScaleMatrixDetected) {
    glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(2.5f));
    EXPECT_TRUE(has_uniform_scale(m));
}

TEST(TransformMath, NonUniformScaleMatrixRejected) {
    glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 1.0f));
    EXPECT_FALSE(has_uniform_scale(m));
}

TEST(TransformMath, RotatedUniformScaleStillUniform) {
    glm::mat4 m = glm::rotate(glm::mat4(1.0f), 1.0f, glm::vec3(0, 1, 0));
    m = glm::scale(m, glm::vec3(3.0f));
    EXPECT_TRUE(has_uniform_scale(m));
}

TEST(TransformMath, TranslationDoesNotAffectScaleCheck) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(100, -20, 5000));
    EXPECT_TRUE(has_uniform_scale(m));
}

TEST(TransformMath, NormalMatrixFastPathReturnsModelForUniformScale) {
    glm::mat4 m = glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(1, 1, 0));
    m = glm::scale(m, glm::vec3(1.5f));
    glm::mat4 n = compute_normal_matrix(m);
    // Fast path: should return model itself unchanged.
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) EXPECT_NEAR(n[i][j], m[i][j], 1e-5f);
    }
}

TEST(TransformMath, NormalMatrixSlowPathForNonUniformScale) {
    glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 0.5f));
    glm::mat4 n = compute_normal_matrix(m);
    glm::mat4 expected = glm::transpose(glm::inverse(m));
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) EXPECT_NEAR(n[i][j], expected[i][j], 1e-4f);
    }
}

TEST(TransformMath, MaxScaleFactorOfIdentity) {
    EXPECT_FLOAT_EQ(max_scale_factor(glm::mat4(1.0f)), 1.0f);
}

TEST(TransformMath, MaxScaleFactorOfUniformScale) {
    glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(4.5f));
    EXPECT_NEAR(max_scale_factor(m), 4.5f, 1e-4f);
}

TEST(TransformMath, MaxScaleFactorOfNonUniformScalePicksLargest) {
    glm::mat4 m = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 7.0f, 3.0f));
    EXPECT_NEAR(max_scale_factor(m), 7.0f, 1e-4f);
}

TEST(TransformMath, MaxScaleFactorIgnoresTranslation) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(1000, 0, -500));
    m = glm::scale(m, glm::vec3(2.0f));
    EXPECT_NEAR(max_scale_factor(m), 2.0f, 1e-4f);
}

TEST(TransformMath, MaxScaleFactorPreservesRotation) {
    // Pure rotation has scale = 1 on all axes regardless of angle.
    glm::mat4 m = glm::rotate(glm::mat4(1.0f), 1.2345f, glm::vec3(0.5f, 0.8f, 0.2f));
    EXPECT_NEAR(max_scale_factor(m), 1.0f, 1e-4f);
}
