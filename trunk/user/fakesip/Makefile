include $(ROOTDIR)/rules.mk

PKG_NAME:=fakesip
PKG_VERSION:=0.9.1

PKG_SOURCE:=fakesip-linux-mips32el.tar.gz
PKG_SOURCE_URL:=https://github.com/MikeWang000000/FakeSIP/releases/download/$(PKG_VERSION)/
PKG_HASH:=f3d8480108eea6358eb15652c68a07161ec224a17f3226bd369e6bba25d877da

PKG_BUILD_DIR:=$(BUILD_DIR)/fakesip-linux-mips32el

include $(INCLUDE_DIR)/package.mk

define Build/Configure
endef

define Build/Compile
endef

$(eval $(call BuildPackage,fakesip))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fakesip $(ROMFSDIR)/usr/bin/fakesip
