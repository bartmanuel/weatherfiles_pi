# ---------------------------------------------------------------------------
# Author:      Jon Gough Copyright:   2020 License:     GPLv3+
# ---------------------------------------------------------------------------

# This file contains changes needed during the make package process depending on the type of package being created

if(CPACK_GENERATOR MATCHES "DEB")
    set(CPACK_PACKAGE_FILE_NAME "weatherfiles_pi-0.1.0.0-darwin-26.5")
    if(CPACK_DEBIAN_PACKAGE_ARCHITECTURE MATCHES "x86_64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE )
    endif()
else()
    set(CPACK_PACKAGE_FILE_NAME "weatherfiles_pi-0.1.0.0-darwin-arm64;x86_64-26.5")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE arm64;x86_64)
endif()
