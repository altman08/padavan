include $(ROOTDIR)/rules.mk

PKG_NAME:=xray
PKG_VERSION:=26.11.9

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/XTLS/Xray-core.git
PKG_SOURCE_VERSION:=v$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

# Set to n in the product configuration to build full Xray.
CONFIG_FIRMWARE_INCLUDE_XRAY_LITE ?= y
XRAY_BUILD_MODE:=$(if $(filter y,$(CONFIG_FIRMWARE_INCLUDE_XRAY_LITE)),lite,full)
XRAY_GO_TAGS:=$(if $(filter lite,$(XRAY_BUILD_MODE)),-tags=xray_lite)
XRAY_MODE_STAMP:=$(PKG_BUILD_DIR)/.xray-mode-$(XRAY_BUILD_MODE)

XRAY_GO_OVERLAY:=
ifeq ($(CONFIG_SOC_MT7621),y)
ifneq ($(CONFIG_FIRMWARE_INCLUDE_EIP93),n)
XRAY_GO_OVERLAY:=-overlay=$(PKG_BUILD_DIR)/.eip93-overlay/overlay.json
endif
endif

define Build/Configure
endef

define Build/Compile
	$(if $(XRAY_GO_OVERLAY),python3 $(CURDIR)/eip93/overlay.py --go go --out $(PKG_BUILD_DIR)/.eip93-overlay)
	(cd $(PKG_BUILD_DIR); \
		GOOS=linux GOARCH=mipsle CGO_ENABLED=1 CC=$(CCACHE_WRAPPER_DIR)/$(TARGET_CC_NOCACHE) \
		go build $(XRAY_GO_TAGS) $(XRAY_GO_OVERLAY) -v -o xray \
		-buildvcs=false -gcflags="-l=4" \
		-trimpath -ldflags "-X github.com/xtls/xray-core/core.build=REPLACE -s -w -buildid=" \
		./main; \
	)
endef

$(eval $(call BuildPackage,xray))

# Re-prepare existing source trees when the copied patch changes.
$(STAMP_PREPARED): $(CURDIR)/Makefile $(wildcard $(CURDIR)/patches/*.patch)

# Track command-line overrides too, even when the product config is unchanged.
$(XRAY_MODE_STAMP): $(STAMP_PREPARED)
	rm -f $(PKG_BUILD_DIR)/.xray-mode-*
	touch $@

$(STAMP_BUILT): $(XRAY_MODE_STAMP) $(CURDIR)/Makefile $(wildcard $(CURDIR)/eip93/* $(PROJECT_CONFIG) $(LINUX_CONFIG))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xray $(ROMFSDIR)/usr/bin/xray
