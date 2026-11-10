include $(ROOTDIR)/rules.mk

PKG_NAME:=jq
PKG_VERSION:=1.8.2

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://github.com/jqlang/jq/releases/download/$(PKG_NAME)-$(PKG_VERSION)/
PKG_HASH:=71b8d6e8f5fe81f6c6d0d110e3892251f6ce76ed095abd315e26e6e1193af3af

include $(INCLUDE_DIR)/package.mk

CONFIGURE_ARGS += --with-oniguruma=builtin

$(eval $(call BuildPackage,jq))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/lib
	$(CP) $(PKG_BUILD_DIR)/.libs/libjq.so* $(ROMFSDIR)/lib/
	$(CP) $(PKG_BUILD_DIR)/vendor/oniguruma/src/.libs/libonig.so* $(ROMFSDIR)/lib/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/.libs/jq $(ROMFSDIR)/usr/bin/jq
