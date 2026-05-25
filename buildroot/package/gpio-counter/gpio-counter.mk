################################################################################
#
# gpio-counter
#
################################################################################

GPIO_COUNTER_VERSION = 1.0
GPIO_COUNTER_SITE_METHOD = local
GPIO_COUNTER_SITE = $(BR2_EXTERNAL_BUILDROOT_SUBMODULE_PATH)/package/gpio-counter/src
$(eval $(kernel-module))
$(eval $(generic-package))
