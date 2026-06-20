################################################################################
#
# botshell — BotOS Shell (BotShell)
#
################################################################################
#
# Buildroot package recipe for the BotOS interactive shell.
# BotShell is a POSIX-compatible command interpreter with
# built-in PyBridge (Python bridge) support.
#
################################################################################

BOTSHELL_VERSION = 0.3.0
BOTSHELL_SITE = $(TOPDIR)/../shell
BOTSHELL_SITE_METHOD = local
BOTSHELL_LICENSE = MIT
BOTSHELL_LICENSE_FILES = ../LICENSE

BOTSHELL_INSTALL_STAGING = YES
BOTSHELL_INSTALL_TARGET = YES

# Dependencies
BOTSHELL_DEPENDENCIES = python3 host-cmake
BOTSHELL_CONF_OPTS = \
	-DBOTOS_ENABLE_PYBRIDGE=ON \
	-DCMAKE_INSTALL_PREFIX=/usr

# Use CMake build system
$(eval $(cmake-package))
