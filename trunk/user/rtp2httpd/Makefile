include $(ROOTDIR)/rules.mk

PKG_NAME:=rtp2httpd
PKG_VERSION:=3.16.0

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/stackia/rtp2httpd/tar.gz/v$(PKG_VERSION)?
PKG_HASH:=83ff812c5b454cf70057bd2b714c891c8650260d4092d6b86629e451fda57d26

export RELEASE_VERSION=$(PKG_VERSION)

CMAKE_OPTIONS += -DENABLE_AGGRESSIVE_OPT=ON

# SO_EE_ORIGIN_ZEROCOPY and SO_EE_CODE_ZEROCOPY_COPIED were added in Linux 4.14.
# Older kernel headers in the cross-toolchain lack these definitions, so we
# supply the canonical values here to avoid compile errors.
EXTRA_CFLAGS += -DSO_EE_ORIGIN_ZEROCOPY=5 -DSO_EE_CODE_ZEROCOPY_COPIED=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

$(eval $(call BuildPackage,rtp2httpd))

romfs:
	$(TARGET_CROSS)strip $(PKG_BUILD_DIR)/rtp2httpd
	$(INSTALL_DIR) $(ROMFSDIR)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/rtp2httpd $(ROMFSDIR)/usr/bin/rtp2httpd
