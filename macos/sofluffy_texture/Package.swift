// swift-tools-version: 5.9
// The macOS plugin as a Swift package, which the Flutter tool links into an
// app that integrates plugins this way; sofluffy_texture.podspec serves
// CocoaPods apps from the same sources.

import PackageDescription

let package = Package(
    name: "sofluffy_texture",
    platforms: [
        .macOS("10.15")
    ],
    products: [
        .library(name: "sofluffy-texture", targets: ["sofluffy_texture"])
    ],
    dependencies: [
        .package(name: "FlutterFramework", path: "../FlutterFramework")
    ],
    targets: [
        .target(
            name: "sofluffy_texture",
            dependencies: [
                .product(name: "FlutterFramework", package: "FlutterFramework")
            ],
            linkerSettings: [
                .linkedFramework("CoreVideo"),
                .linkedFramework("IOSurface"),
            ]
        )
    ],
    cxxLanguageStandard: .cxx17
)
