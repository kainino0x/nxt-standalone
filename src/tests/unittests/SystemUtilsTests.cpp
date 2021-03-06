// Copyright 2019 The Dawn Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include "common/SystemUtils.h"

// Tests for GetEnvironmentVar
TEST(SystemUtilsTests, GetEnvironmentVar) {
    // Test nonexistent environment variable
    ASSERT_EQ(GetEnvironmentVar("NonexistentEnvironmentVar"), "");
}

// Tests for SetEnvironmentVar
TEST(SystemUtilsTests, SetEnvironmentVar) {
    // Test new environment variable
    ASSERT_TRUE(SetEnvironmentVar("EnvironmentVarForTest", "NewEnvironmentVarValue"));
    ASSERT_EQ(GetEnvironmentVar("EnvironmentVarForTest"), "NewEnvironmentVarValue");
    // Test override environment variable
    ASSERT_TRUE(SetEnvironmentVar("EnvironmentVarForTest", "OverrideEnvironmentVarValue"));
    ASSERT_EQ(GetEnvironmentVar("EnvironmentVarForTest"), "OverrideEnvironmentVarValue");
}

// Tests for GetExecutableDirectory
TEST(SystemUtilsTests, GetExecutableDirectory) {
    // Test returned value is non-empty string
    ASSERT_NE(GetExecutableDirectory(), "");
    // Test last charecter in path
    ASSERT_EQ(GetExecutableDirectory().back(), *GetPathSeparator());
}
