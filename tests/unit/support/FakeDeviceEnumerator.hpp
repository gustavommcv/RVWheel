#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "rvwheel/dal/DeviceManager.hpp"
#include "FakeWheelDevice.hpp"

namespace rvwheel::testing {

// Produces devices from a caller-supplied "recipe" (a list of DeviceInfo),
// creating brand-new FakeWheelDevice instances on every Enumerate() call --
// exactly like a real enumerator would. This lets tests verify that
// DeviceManager itself (not the enumerator) is responsible for recognizing
// an already-known DeviceId and preserving the original instance rather
// than swapping in the new one.
class FakeDeviceEnumerator final : public rvwheel::dal::IDeviceEnumerator {
public:
    explicit FakeDeviceEnumerator(std::vector<rvwheel::dal::DeviceInfo> recipe) : recipe_(std::move(recipe)) {}

    std::vector<std::unique_ptr<rvwheel::dal::IWheelDevice>> Enumerate() override {
        ++enumerateCallCount;
        std::vector<std::unique_ptr<rvwheel::dal::IWheelDevice>> result;
        result.reserve(recipe_.size());
        for (const auto& info : recipe_) {
            result.push_back(std::make_unique<FakeWheelDevice>(info));
        }
        return result;
    }

    void SetRecipe(std::vector<rvwheel::dal::DeviceInfo> recipe) { recipe_ = std::move(recipe); }

    int enumerateCallCount = 0;

private:
    std::vector<rvwheel::dal::DeviceInfo> recipe_;
};

} // namespace rvwheel::testing
