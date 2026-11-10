include $(ROOTDIR)/rules.mk

PKG_NAME:=tmux
PKG_VERSION:=3.8

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.gz
PKG_SOURCE_URL:=https://codeload.github.com/tmux/tmux/tar.gz/$(PKG_VERSION)?
PKG_HASH:=f873f9379c9cf30d3d66b642a955118c4fbb0019d046d13e6012340811e5dcb7

include $(INCLUDE_DIR)/package.mk

define TmuxPreConfigure
	(cd $(PKG_BUILD_DIR); \
	sh autogen.sh; \
	)
endef

Hooks/Configure/Pre += TmuxPreConfigure

$(eval $(call BuildPackage,tmux))

romfs:
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/tmux $(ROMFSDIR)/usr/bin/tmux
