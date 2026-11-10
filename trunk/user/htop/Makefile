include $(ROOTDIR)/rules.mk

PKG_NAME:=htop
PKG_VERSION:=3.5.3

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/htop-dev/htop/tar.gz/$(PKG_VERSION)?
PKG_HASH:=edf25ee020a5263ffbef9eef5a8c14392bf74e78b3d5c8bc64d9343dd9a82605

PKG_FIXUP:=autoreconf

include $(INCLUDE_DIR)/package.mk

CONFIGURE_ARGS += \
	--enable-unicode \
	HTOP_NCURSESW_CONFIG_SCRIPT="$(STAGEDIR)/bin/ncursesw6-config"

$(eval $(call BuildPackage,htop))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/htop $(ROMFSDIR)/usr/bin/htop
