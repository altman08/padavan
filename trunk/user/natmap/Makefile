include $(ROOTDIR)/rules.mk

PKG_NAME:=natmap
PKG_VERSION:=20260214

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.xz
PKG_SOURCE_URL:=https://github.com/heiher/natmap/releases/download/$(PKG_VERSION)
PKG_HASH:=1562f16b2e222690c32ed6f69d25aa7a59a4319d00d63b1bd4b98010db035159

include $(INCLUDE_DIR)/package.mk
MAKE_FLAGS = CROSS_PREFIX=$(CCACHE_WRAPPER_DIR)/$(TARGET_CROSS) ENABLE_STATIC=1

$(eval $(call BuildPackage,natmap))

romfs:
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/bin/natmap $(ROMFSDIR)/usr/bin/natmap
