include $(ROOTDIR)/rules.mk

PKG_NAME:=socat
PKG_VERSION:=1.8.1.1

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
PKG_SOURCE_URL:=http://www.dest-unreach.org/socat/download
PKG_HASH:=5ebc636b7f427053f98806696521653a614c7e06464910353cbf54e2327adc1b

include $(INCLUDE_DIR)/package.mk

CONFIGURE_ARGS += \
	--disable-openssl \
	--disable-readline

$(eval $(call BuildPackage,socat))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/socat $(ROMFSDIR)/usr/bin/socat
