#include <gtest/gtest.h>
#include "engine/model_loader.hpp"

using namespace mmo::engine;

TEST(ModelManager, InvalidHandleIsZero) {
    EXPECT_EQ(INVALID_MODEL_HANDLE, 0u);
}

TEST(ModelManager, GetModelInvalidHandleReturnsNull) {
    ModelManager mgr;
    EXPECT_EQ(mgr.get_model(INVALID_MODEL_HANDLE), nullptr);
}

TEST(ModelManager, GetModelUnknownHandleReturnsNull) {
    ModelManager mgr;
    EXPECT_EQ(mgr.get_model(static_cast<ModelHandle>(999)), nullptr);
}

TEST(ModelManager, GetHandleUnknownNameReturnsInvalid) {
    ModelManager mgr;
    EXPECT_EQ(mgr.get_handle("nonexistent"), INVALID_MODEL_HANDLE);
}

TEST(ModelManager, GetModelByUnknownNameReturnsNull) {
    ModelManager mgr;
    EXPECT_EQ(mgr.get_model(std::string("nonexistent")), nullptr);
}
