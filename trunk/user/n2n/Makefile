include $(ROOTDIR)/rules.mk

PKG_NAME:=n2n

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/ntop/n2n.git
PKG_SOURCE_VERSION:=3.1.1

CMAKE_OPTIONS += -DN2N_OPTION_USE_PTHREAD=ON
CMAKE_OPTIONS += -DN2N_OPTION_USE_OPENSSL=ON

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

$(eval $(call BuildPackage,n2n))

romfs:
	$(TARGET_CROSS)strip $(PKG_BUILD_DIR)/supernode
	$(TARGET_CROSS)strip $(PKG_BUILD_DIR)/edge
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/supernode $(ROMFSDIR)/usr/bin/supernode
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/edge $(ROMFSDIR)/usr/bin/edge
