include $(ROOTDIR)/rules.mk

PKG_NAME:=xray
PKG_VERSION:=26.10.21

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/XTLS/Xray-core.git
PKG_SOURCE_VERSION:=v$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

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
		go build $(XRAY_GO_OVERLAY) -v -o xray \
		-buildvcs=false -gcflags="-l=4" \
		-trimpath -ldflags "-X github.com/xtls/xray-core/core.build=REPLACE -s -w -buildid=" \
		./main; \
	)
endef

$(eval $(call BuildPackage,xray))

$(STAMP_BUILT): $(CURDIR)/Makefile $(wildcard $(CURDIR)/eip93/* $(PROJECT_CONFIG) $(LINUX_CONFIG))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xray $(ROMFSDIR)/usr/bin/xray
