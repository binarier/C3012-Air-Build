################################################################################
#
# cgminer
#
################################################################################
CGMINER_VERSION = $(call qstrip,$(BR2_PACKAGE_CGMINER_VERSION))
CGMINER_SITE_METHOD = git
CGMINER_SITE = https://github.com/binarier/cgminer.git
CGMINER_DEPENDENCIES = host-pkgconf jansson libcurl
CGMINER_AUTORECONF = YES
CGMINER_CONF_OPTS = --enable-clover-rust --with-system-jansson 

define BUILD_CGMINER_API
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) -o $(@D)/cgminer-api \
		$(@D)/api-example.c
endef

define CGMINER_POST_EXTRACT
	echo "$(CGMINER_VERSION)" > $(@D)/cgminer_git.hash
endef

CGMINER_POST_BUILD_HOOKS += BUILD_CGMINER_API
#CGMINER_POST_EXTRACT_HOOKS += CGMINER_POST_EXTRACT
CGMINER_CONF_ENV += CFLAGS="$(TARGET_CFLAGS) -fcommon"

define INSTALL_CGMINER_API
	$(INSTALL) -D -m 755 $(@D)/cgminer-api \
		$(TARGET_DIR)/usr/bin/cgminer-api
endef

define INSTALL_CGMINER_VERSION
	$(INSTALL) -D -m 0644 $(@D)/cgminer_git.hash \
                $(TARGET_DIR)/etc/cgminer_git.hash
endef

CGMINER_POST_INSTALL_TARGET_HOOKS += INSTALL_CGMINER_API 

$(eval $(autotools-package))
