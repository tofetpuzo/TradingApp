// TODO: Story 2 (#19)
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace trading 
{

using Price = double;
using Quantity = double;
using OrderId = uint64_t;
using timestamp = std::chrono::steady_clock::time_point;


}  // namespace trading