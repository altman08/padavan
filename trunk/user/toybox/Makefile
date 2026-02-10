include $(ROOTDIR)/rules.mk

PKG_NAME:=toybox

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/landley/toybox.git
PKG_SOURCE_VERSION:=0.8.13
THISDIR = $(shell pwd)

include $(INCLUDE_DIR)/package.mk

MAKE_FLAGS = CROSS_COMPILE=$(CCACHE_WRAPPER_DIR)/ PREFIX=$(ROMFSDIR)

define Build/Configure
	(cd $(PKG_BUILD_DIR); \
	  $(MAKE) $(MAKE_FLAGS) defconfig; \
	)
endef

define Build/Compile
	cp -f $(THISDIR)/RM2100.conf .config;
	$(MAKE) $(MAKE_FLAGS)
endef

$(eval $(call BuildPackage,toybox))

romfs:
	$(MAKE) -C $(PKG_BUILD_DIR) $(MAKE_FLAGS) install