#pragma once

#include "poison_package_catalog.h"
#include "poison_package_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

bool poison_package_catalog_from_manager(
    PoisonPackageCatalog* catalog,
    const PoisonPackageManager* manager,
    const PoisonPackageAuthorityStore* authorities);

#ifdef __cplusplus
}
#endif
