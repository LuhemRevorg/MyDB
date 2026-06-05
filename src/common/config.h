#pragma once
#include <cstdint>

static constexpr int PAGE_SIZE = 4096;  // 4 KB — matches OS page size

using page_id_t  = int32_t;
using frame_id_t = int32_t;

static constexpr page_id_t INVALID_PAGE_ID = -1;
