#include "BridgeStateFormatter.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace rvwheel::tools::probe {

namespace {

[[nodiscard]] float Sanitize(float value, float minimum, float maximum) noexcept {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp(value, minimum, maximum);
}

} // namespace

std::string BridgeStateFormatter::Format(const BridgeStateRecord& record) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "RVW2 " << record.sequence << ' ' << (record.connected ? 1 : 0) << ' ' << (record.valid ? 1 : 0) << ' '
           << std::fixed << std::setprecision(6) << Sanitize(record.steering, -1.0f, 1.0f) << ' '
           << Sanitize(record.throttle, 0.0f, 1.0f) << ' ' << Sanitize(record.brake, 0.0f, 1.0f) << ' '
           << Sanitize(record.clutch, 0.0f, 1.0f);
    output << std::uppercase << std::hex << std::setfill('0');
    output << ' ' << std::setw(4) << record.vendorId << ' ' << std::setw(4) << record.productId;
    for (const std::uint32_t word : record.buttonWords) {
        output << ' ' << std::setw(8) << word;
    }
    output << std::dec << std::setfill(' ') << ' ' << record.sequence << '\n';
    return output.str();
}

} // namespace rvwheel::tools::probe
