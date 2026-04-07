# ─────────────────────────────────────────────────────────────────────────────
# Created by Cadmus of Tyre (@ccot7) on 4/7/26.
#
# Makefile — CadmusPwr macOS
#
# Automates building, installing, and cleaning the app using xcodebuild.
#
# Usage:
#   make            — build a Release .app bundle
#   make debug      — build a Debug .app bundle
#   make install    — build + copy to /Applications
#   make sudoers    — install the sudoers entry so powermetrics runs without password
#   make clean      — remove build artefacts
#   make help       — show this message
#
# Note on paths:
#   xcodebuild with -derivedDataPath puts the .app at:
#     <derivedDataPath>/Build/Products/<Configuration>/<AppName>.app
#   NOT at <derivedDataPath>/<Configuration>/<AppName>.app
#   RELEASE_DIR and APP_BUNDLE reflect the actual output location.
# ─────────────────────────────────────────────────────────────────────────────

APP_NAME    := CadmusPwr
SCHEME      := $(APP_NAME)
PROJECT     := $(APP_NAME).xcodeproj
# CC Added: 
# Use a hidden directory (starting with '.') for builds. 
# This prevents macOS Launch Services and Spotlight from indexing 
# the intermediate .app bundles, which stops "duplicate" versions 
# from appearing in Finder, Launchpad, and 'Open With' menus.
BUILD_DIR := .build

# xcodebuild puts products here, not directly under BUILD_DIR
PRODUCTS_DIR := $(BUILD_DIR)/Build/Products
RELEASE_DIR  := $(PRODUCTS_DIR)/Release
DEBUG_DIR    := $(PRODUCTS_DIR)/Debug
APP_BUNDLE   := $(RELEASE_DIR)/$(APP_NAME).app
INSTALL_DIR  := /Applications

# Detect the current user for the sudoers entry
CURRENT_USER := $(shell whoami)

.PHONY: all debug install sudoers clean help

## Build a Release .app bundle (default target)
all:
	xcodebuild \
	  -project         "$(PROJECT)" \
	  -scheme          "$(SCHEME)"  \
	  -configuration   Release      \
	  -derivedDataPath "$(BUILD_DIR)" \
	  build
	@echo ""
	@echo "✓  Built: $(APP_BUNDLE)"

## Build a Debug .app bundle (faster, includes debug symbols for lldb)
debug:
	xcodebuild \
	  -project         "$(PROJECT)" \
	  -scheme          "$(SCHEME)"  \
	  -configuration   Debug        \
	  -derivedDataPath "$(BUILD_DIR)" \
	  build
	@echo ""
	@echo "✓  Debug build: $(DEBUG_DIR)/$(APP_NAME).app"

## Build Release and copy the .app to /Applications
install: all
	@echo "Installing $(APP_NAME).app to $(INSTALL_DIR)..."
	@# Remove any existing installation first to avoid stale files
	@rm -rf "$(INSTALL_DIR)/$(APP_NAME).app"
	cp -R "$(APP_BUNDLE)" "$(INSTALL_DIR)/"
	@echo "✓  Installed to $(INSTALL_DIR)/$(APP_NAME).app"
	@echo ""
	@echo "Tip: run 'make sudoers' if you haven't set up passwordless powermetrics yet."

## Write /etc/sudoers.d/CadmusPwr so powermetrics runs without a password prompt.
## powermetrics requires root — this targets only that binary, nothing else.
sudoers:
	@echo "Installing sudoers entry for $(CURRENT_USER)..."
	@echo "$(CURRENT_USER) ALL=(ALL) NOPASSWD: /usr/bin/powermetrics" \
	  | sudo tee /etc/sudoers.d/CadmusPwr > /dev/null
	@sudo chmod 400 /etc/sudoers.d/CadmusPwr
	@echo "✓  Sudoers entry written to /etc/sudoers.d/CadmusPwr (chmod 400)"
	@echo "   Verify: sudo powermetrics --samplers cpu_power -i 1000 -n 1"

## Remove all build artefacts
clean:
	rm -rf "$(BUILD_DIR)"
	@echo "✓  Removed $(BUILD_DIR)/"

## Print available targets
help:
	@echo ""
	@echo "CadmusPwr macOS — make targets:"
	@echo "  make            build Release .app → $(RELEASE_DIR)/"
	@echo "  make debug      build Debug   .app → $(DEBUG_DIR)/"
	@echo "  make install    build Release + copy to $(INSTALL_DIR)"
	@echo "  make sudoers    write /etc/sudoers.d/CadmusPwr (run once)"
	@echo "  make clean      remove $(BUILD_DIR)/"
	@echo "  make help       show this message"
	@echo ""
