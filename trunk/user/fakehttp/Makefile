include $(ROOTDIR)/rules.mk

PKG_NAME:=fakehttp
PKG_VERSION:=0.9.18

PKG_SOURCE:=fakehttp-linux-mips32el.tar.gz
PKG_SOURCE_URL:=https://github.com/MikeWang000000/FakeHTTP/releases/download/$(PKG_VERSION)/
PKG_HASH:=d2aef2ac86fb93e5d4d7f1702d62441f9fd60f350f737e9798213b2e2918d6a9

PKG_BUILD_DIR:=$(BUILD_DIR)/fakehttp-linux-mips32el

include $(INCLUDE_DIR)/package.mk

define Build/Configure
endef

define Build/Compile
endef

$(eval $(call BuildPackage,fakehttp))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fakehttp $(ROMFSDIR)/usr/bin/fakehttp
