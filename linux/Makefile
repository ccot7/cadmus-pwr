# ─────────────────────────────────────────────────────────────────────────────
# Created by Cadmus of Tyre (@ccot7) on 4/8/26.
# Makefile — CadmusPwr Linux (GTK3)
#
# Usage:
#   make            — compile the binary
#   make install    — compile + copy to ~/.local/bin
#   make desktop    — install .desktop entry (shows in GNOME launcher)
#   make udev       — install permanent RAPL permissions (run once)
#   make uninstall  — remove binary and .desktop entry
#   make clean      — remove build artefacts
#   make help       — show this message
#
# Note on build directory:
#   Uses .build/ (hidden) so Finder/Spotlight equivalents on shared filesystems
#   and IDE project scanners don't index intermediate build files.
# ─────────────────────────────────────────────────────────────────────────────

CC      := gcc
CFLAGS  := -O2 -Wall -Wextra -Wpedantic -Wformat=2 \
           $(shell pkg-config --cflags gtk+-3.0)
LDFLAGS := $(shell pkg-config --libs gtk+-3.0) -lm

APP_NAME    := CadmusPwr
SRC         := CadmusPwr.c
BUILD_DIR   := .build
TARGET      := $(BUILD_DIR)/$(APP_NAME)
INSTALL_BIN := $(HOME)/.local/bin/$(APP_NAME)
DESKTOP_DIR := $(HOME)/.local/share/applications
DESKTOP_DST := $(DESKTOP_DIR)/CadmusPwr.desktop
UDEV_RULE   := /etc/udev/rules.d/99-rapl.rules
CURRENT_USER := $(shell whoami)

.PHONY: all install desktop udev uninstall clean help

## Compile the binary into .build/
all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	@echo ""
	@echo "✓  Built: $(TARGET)"

## Compile and copy the binary to ~/.local/bin
install: all
	mkdir -p $(HOME)/.local/bin
	cp $(TARGET) $(INSTALL_BIN)
	@echo "✓  Installed to $(INSTALL_BIN)"
	@echo ""
	@echo "Tip: run 'make desktop' to add to GNOME app launcher."
	@echo "Tip: run 'make udev'   to set permanent RAPL permissions."

## Install .desktop entry so the app appears in GNOME Activities
ICON_DIR := $(HOME)/.local/share/icons
ICON_FULL_PATH := $(ICON_DIR)/hicolor/512x512/apps

install-icon:
	mkdir -p $(ICON_FULL_PATH)
	cp CadmusPwr.png $(ICON_FULL_PATH)/CadmusPwr.png

desktop: install-icon
	mkdir -p $(DESKTOP_DIR)
	echo "[Desktop Entry]" > $(DESKTOP_DST)
	echo "Name=CadmusPwr" >> $(DESKTOP_DST)
	echo "Comment=CPU, Power and thermal monitor" >> $(DESKTOP_DST)
	echo "Exec=$(INSTALL_BIN)" >> $(DESKTOP_DST)
	echo "Icon=$(ICON_FULL_PATH)/CadmusPwr.png" >> $(DESKTOP_DST) ## will be taken from system:~/.local/share/icons/hicolor/512x512/apps/CadmusPwr.png
	echo "Terminal=false" >> $(DESKTOP_DST)
	echo "Type=Application" >> $(DESKTOP_DST)
	echo "Categories=Utility;System;Monitor;" >> $(DESKTOP_DST)
	update-desktop-database $(DESKTOP_DIR)/
	@echo "✓ Installed CadmusPwr desktop entry"

## (Important) Refresh icon cache (optional but recommended)
## Some desktops pick it up automatically, but to be safe:
update-icon-cache:
	gtk-update-icon-cache -f -t $(HOME)/.local/share/icons/hicolor

## Install permanent udev rule so RAPL is readable without sudo (run once)
## Without this, you must run the app with sudo every time.
udev:
	@echo 'SUBSYSTEM=="powercap", ACTION=="add", RUN+="/bin/chmod o+r /sys/class/powercap/%k/energy_uj"' \
	  | sudo tee $(UDEV_RULE) > /dev/null
	sudo udevadm control --reload-rules
	sudo udevadm trigger
	@echo "✓  udev rule written to $(UDEV_RULE)"
	@echo "   You can now run ./$(APP_NAME) without sudo."

## Remove binary and .desktop entry
uninstall:
	rm -f $(INSTALL_BIN)
	rm -f $(DESKTOP_DST)
	rm -rf $(ICON_DIR)
	@-update-desktop-database $(DESKTOP_DIR)/ 2>/dev/null || true
	@echo "✓  Uninstalled $(APP_NAME)."

## Remove build artefacts
clean:
	rm -rf $(BUILD_DIR)
	@echo "✓  Removed $(BUILD_DIR)/"

## Print available targets
help:
	@echo ""
	@echo "CadmusPwr Linux — make targets:"
	@echo "  make            compile binary → $(TARGET)"
	@echo "  make install    compile + copy to $(HOME)/.local/bin"
	@echo "  make desktop    install .desktop entry for GNOME launcher"
	@echo "  make udev       permanent RAPL read permissions (run once)"
	@echo "  make uninstall  remove binary and .desktop entry"
	@echo "  make clean      remove $(BUILD_DIR)/"
	@echo "  make help       show this message"
	@echo ""
