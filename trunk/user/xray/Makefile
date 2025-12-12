include $(ROOTDIR)/rules.mk

PKG_NAME:=xray

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/XTLS/Xray-core.git
PKG_SOURCE_VERSION:=v25.12.8

include $(INCLUDE_DIR)/package.mk

define Build/Configure
endef

define Build/Compile
	(cd $(PKG_BUILD_DIR); \
		GOOS=linux GOARCH=mipsle CGO_ENABLED=0 \
		go build -v -o xray \
		-buildvcs=false -gcflags="-l=4" -ldflags="" \
		-trimpath -ldflags "-X github.com/xtls/xray-core/core.build=REPLACE -s -w -buildid=" \
		./main; \
	)
endef

$(eval $(call BuildPackage,xray))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/xray $(ROMFSDIR)/usr/bin/xray
