include $(ROOTDIR)/rules.mk

PKG_NAME:=natmap

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/heiher/natmap.git
PKG_SOURCE_VERSION:=20250924

include $(INCLUDE_DIR)/package.mk
MAKE_FLAGS = CROSS_PREFIX=$(CCACHE_WRAPPER_DIR)/$(TARGET_CROSS) ENABLE_STATIC=1

$(eval $(call BuildPackage,natmap))

romfs:
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/bin/natmap $(ROMFSDIR)/usr/bin/natmap
