include $(ROOTDIR)/rules.mk

PKG_NAME:=quarkdrive-webdav
PKG_VERSION:=1.3.9
PKG_SOURCE_VERSION:=7a88d7f1fa299a6ef75b99d7e5dbc07a717f579e
PKG_SOURCE:=$(PKG_NAME)-$(PKG_SOURCE_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/chenqimiao/quarkdrive-webdav/tar.gz/$(PKG_SOURCE_VERSION)?
PKG_HASH:=6e6457ae3459c41acb112d1848c1d690eca50168da84b17d3eb5bf0143f289df
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_SOURCE_VERSION)

include $(INCLUDE_DIR)/package.mk

QUARK_RUST_TOOLCHAIN ?= 1.94.0
QUARK_EIP93_DISABLED:=$(if $(filter y,$(CONFIG_SOC_MT7621)),$(if $(filter n,$(CONFIG_FIRMWARE_INCLUDE_EIP93)),1,0),1)
QUARK_MODE_STAMP:=$(PKG_BUILD_DIR)/.quark-mode-$(QUARK_RUST_TOOLCHAIN)-$(QUARK_EIP93_DISABLED)

define Build/Configure
endef

define Build/Compile
	QUARK_CC="$(TARGET_CC_NOCACHE)" QUARK_AR="$(TARGET_AR)" \
	QUARK_RUST_TOOLCHAIN="$(QUARK_RUST_TOOLCHAIN)" QUARK_EIP93_DISABLED="$(QUARK_EIP93_DISABLED)" \
		$(CURDIR)/build.sh $(PKG_BUILD_DIR) $(HOST_NCPU)
endef

$(eval $(call BuildPackage,quarkdrive-webdav))

$(STAMP_PREPARED): $(CURDIR)/Makefile $(wildcard $(CURDIR)/patches/*.patch)
$(QUARK_MODE_STAMP): $(STAMP_PREPARED)
	rm -f $(PKG_BUILD_DIR)/.quark-mode-*
	touch $@

$(STAMP_BUILT): $(QUARK_MODE_STAMP) $(CURDIR)/build.sh $(wildcard $(PROJECT_CONFIG) $(LINUX_CONFIG))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/target/mipsel-unknown-linux-musl/release/quarkdrive-webdav $(ROMFSDIR)/usr/bin/
	$(INSTALL_BIN) $(CURDIR)/quarkdrive-webdav.sh $(ROMFSDIR)/usr/bin/
