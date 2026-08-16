// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "MuesliCore",
    platforms: [.iOS(.v16), .macOS(.v13)],
    products: [
        .library(name: "MuesliCore", targets: ["MuesliCore"])
    ],
    targets: [
        .target(name: "MuesliCore"),
        .testTarget(name: "MuesliCoreTests", dependencies: ["MuesliCore"]),
    ]
)
