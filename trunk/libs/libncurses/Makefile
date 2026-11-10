include $(ROOTDIR)/rules.mk

PKG_NAME:=ncurses
PKG_VERSION:=6.6.20260801

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/ThomasDickey/ncurses-snapshots.git
PKG_SOURCE_VERSION:=v$(subst .,_,$(PKG_VERSION))
PKG_MIRROR_HASH:=0b2ecc647ad654286153880aa843e886d5f57f0ed9993acd258d091484a71686

include $(INCLUDE_DIR)/package.mk

CONFIGURE_ARGS += \
	--with-terminfo-dirs=/usr/share/terminfo \
	--with-default-terminfo-dir=/usr/share/terminfo \
	--with-pkg-config-libdir=/lib/pkgconfig \
	--without-ada \
	--without-termlib \
	--without-progs \
	--without-tests \
	--without-manpages \
	--enable-widec \
	--with-shared \
	--without-debug \
	--without-profile \
	--enable-overwrite \
	--enable-pc-files

define Build/Install
	$(call Build/Install/Default,install.libs)
endef

$(eval $(call BuildPackage,ncurses))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/lib
	$(CP) $(PKG_INSTALL_DIR)/lib/libncursesw.so* $(ROMFSDIR)/lib/
