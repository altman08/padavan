include $(ROOTDIR)/rules.mk

PKG_NAME:=dropbear
PKG_VERSION:=2026.94

PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
PKG_SOURCE_URL:= \
	https://matt.ucc.asn.au/dropbear/releases/ \
	https://dropbear.nl/mirror/releases/
PKG_HASH:=e098034a843699200c8c977a991fff73159735bf795d5f72ef672c41a6b1ae81

include $(INCLUDE_DIR)/package.mk

# Allow inetd-style connections via dropbear -i.
TARGET_CFLAGS += -DINETD_MODE=1

ifeq ($(CONFIG_FIRMWARE_INCLUDE_SFTP),y)
TARGET_CFLAGS += -DSFTPSERVER_PATH=\\\"/usr/libexec/sftp-server\\\"
else
TARGET_CFLAGS += -DSFTPSERVER_PATH=\\\"/opt/libexec/sftp-server\\\"
endif
ifneq ($(CONFIG_FIRMWARE_INCLUDE_DROPBEAR_FAST_CODE),y)
TARGET_CFLAGS += -DDROPBEAR_SMALL_CODE
endif

CONFIGURE_ARGS += \
	--disable-lastlog \
	--disable-utmp \
	--disable-utmpx \
	--disable-wtmp \
	--disable-wtmpx \
	--disable-loginfunc \
	--disable-pututline \
	--disable-pututxline \
	--disable-harden \
	--enable-zlib \
	--enable-bundled-libtom

MAKE_FLAGS += PROGRAMS="dropbear dbclient dropbearkey dropbearconvert scp" MULTI=1

$(eval $(call BuildPackage,dropbear))

romfs:
	$(INSTALL_DIR) $(ROMFSDIR)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/dropbearmulti $(ROMFSDIR)/usr/bin/dropbearmulti
	$(INSTALL_BIN) ./sshd.sh $(ROMFSDIR)/usr/bin/sshd.sh
	$(LN) dropbearmulti $(ROMFSDIR)/usr/bin/ssh
	$(LN) dropbearmulti $(ROMFSDIR)/usr/bin/scp
	$(LN) dropbearmulti $(ROMFSDIR)/usr/bin/dropbearkey
	$(LN) dropbearmulti $(ROMFSDIR)/usr/bin/dropbearconvert
	$(INSTALL_DIR) $(ROMFSDIR)/usr/sbin
	$(LN) ../bin/dropbearmulti $(ROMFSDIR)/usr/sbin/dropbear
