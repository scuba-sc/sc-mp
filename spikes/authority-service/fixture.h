// SPDX-License-Identifier: GPL-3.0-only
// Private in-process test bridge; not an SDK service or public ABI.
#pragma once
#include <cstdint>
#include "sco_api.h"
struct FixtureControl {
 uint32_t size;
 bool (*configure)(bool host, bool local, const char* epoch, void (*emit)(const char*, const char*, const char*, uint64_t, uint64_t, int64_t, uint64_t, uint64_t));
 void (*submit)(const char* line);
 void (*tick)(uint64_t now);
 bool (*loaded)();
};
const FixtureControl* fixture_control();
const sco_plugin_info* authority_query();
sco_result authority_load(const sco_api*, sco_plugin*);
void authority_unload();
