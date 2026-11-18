// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "texture_demo",
    platforms: [
        .macOS("10.15")
    ],
    products: [
        .library(name: "texture-demo", targets: ["texture_demo"])
    ],
    dependencies: [
        .package(name: "FlutterFramework", path: "../FlutterFramework"),
        // Another plugin's package sits next to this one, by its name.
        .package(name: "sofluffy_texture", path: "../sofluffy_texture"),
    ],
    targets: [
        .target(
            name: "texture_demo",
            dependencies: [
                .product(name: "FlutterFramework", package: "FlutterFramework"),
                .product(name: "sofluffy-texture", package: "sofluffy_texture"),
            ]
        )
    ]
)
