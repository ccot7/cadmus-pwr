// Created by Cadmus of Tyre (@ccot7) on 4/7/26.
// CadmusPwrApp.swift
// ─────────────────────────────────────────────────────────────────────────────
// App entry point. Sets the window title from AppConstants so renaming
// the app only ever requires editing AppConstants.swift.
// ─────────────────────────────────────────────────────────────────────────────

import SwiftUI

@main
struct CadmusPwrApp: App {

    var body: some Scene {
        WindowGroup {
            ContentView()
                // Title bar uses the constant — change AppConstants.appName
                // and it updates here automatically.
                .navigationTitle(AppConstants.appName)
        }
        // Remove the default "New Window" menu item — single-window app.
        .commands {
            CommandGroup(replacing: .newItem) { }
        }
    }
}
