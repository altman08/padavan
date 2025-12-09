include $(ROOTDIR)/rules.mk

PKG_NAME:=jq

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/jqlang/jq.git
PKG_SOURCE_VERSION:=jq-1.8.1

include $(INCLUDE_DIR)/package.mk

define JqPreConfigure
	(cd $(PKG_BUILD_DIR); \
	autoreconf -i; \
	)
endef

Hooks/Configure/Pre += JqPreConfigure
CONFIGURE_ARGS += --with-oniguruma=builtin

$(eval $(call BuildPackage,jq))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/lib
	$(CP) $(PKG_BUILD_DIR)/.libs/libjq.so* $(ROMFSDIR)/lib/
	$(CP) $(PKG_BUILD_DIR)/vendor/oniguruma/src/.libs/libonig.so* $(ROMFSDIR)/lib/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/.libs/jq $(ROMFSDIR)/usr/bin/jq
