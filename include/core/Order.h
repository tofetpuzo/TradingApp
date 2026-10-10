// TODO: Story 2 (#19) - Order.h
/*
 * Copyright 2026 [TradeX]
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <string>

#include "core/Types.h"

namespace trading {

struct Order {
  order_id id{};
  std::string symbol;
  Side side{Side::buy};
  price limit_price{};
  quantity order_quantity{};
  timestamp submitted_at{};
};

}  // namespace trading