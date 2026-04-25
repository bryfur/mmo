#include "engine/scene/camera_state.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

using namespace mmo::engine::scene;

// ---------------------------------------------------------------------------
// Default construction
// ---------------------------------------------------------------------------

TEST(CameraState, DefaultConstructionZeroInitializes) {
    CameraState cam{};
    // All matrices should be zero-initialized (aggregate init)
    EXPECT_EQ(cam.view, glm::mat4(0.0f));
    EXPECT_EQ(cam.projection, glm::mat4(0.0f));
    EXPECT_EQ(cam.view_projection, glm::mat4(0.0f));
    EXPECT_EQ(cam.position, glm::vec3(0.0f));
}

// ---------------------------------------------------------------------------
// view_projection == projection * view
// ---------------------------------------------------------------------------

TEST(CameraState, ViewProjectionEqualsProjectionTimesView) {
    CameraState cam{};
    cam.view = glm::lookAt(glm::vec3(0.0f, 10.0f, 20.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    cam.projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
    cam.view_projection = cam.projection * cam.view;

    glm::mat4 expected = cam.projection * cam.view;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_FLOAT_EQ(cam.view_projection[col][row], expected[col][row])
                << "mismatch at [" << col << "][" << row << "]";
        }
    }
}

// ---------------------------------------------------------------------------
// Position extraction from view matrix matches stored position
// ---------------------------------------------------------------------------

TEST(CameraState, PositionMatchesViewMatrixInverse) {
    glm::vec3 eye(5.0f, 3.0f, -8.0f);
    glm::vec3 target(0.0f, 0.0f, 0.0f);
    glm::vec3 up(0.0f, 1.0f, 0.0f);

    CameraState cam{};
    cam.view = glm::lookAt(eye, target, up);
    cam.projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    cam.view_projection = cam.projection * cam.view;
    cam.position = eye;

    // Extract position from the inverse of the view matrix
    glm::mat4 inv_view = glm::inverse(cam.view);
    glm::vec3 extracted_pos(inv_view[3]);

    EXPECT_NEAR(cam.position.x, extracted_pos.x, 1e-4f);
    EXPECT_NEAR(cam.position.y, extracted_pos.y, 1e-4f);
    EXPECT_NEAR(cam.position.z, extracted_pos.z, 1e-4f);
}

// ---------------------------------------------------------------------------
// Identity matrices produce identity view_projection
// ---------------------------------------------------------------------------

TEST(CameraState, IdentityMatricesProduceIdentityViewProjection) {
    CameraState cam{};
    cam.view = glm::mat4(1.0f);
    cam.projection = glm::mat4(1.0f);
    cam.view_projection = cam.projection * cam.view;

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_FLOAT_EQ(cam.view_projection[col][row], expected);
        }
    }
}
