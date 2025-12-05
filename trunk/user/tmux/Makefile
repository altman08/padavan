include $(ROOTDIR)/rules.mk

PKG_NAME:=tmux

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/tmux/tmux.git
PKG_SOURCE_VERSION:=2.9

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
