// build.zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    // Define the target executable
    const exe = b.addExecutable(.{
        .name = "Webserver_Challenge", // Name of the output executable
        .target = b.standardTargetOptions(.{}), // Allow cross-compilation
        .optimize = b.standardOptimizeOption(.{}), // Optimization level
    });
    exe.addCSourceFile(.{
        .file = b.path("src/main.c"),
        .flags = &[_][]const u8{},
    });

    // Add include paths so the compiler can find Arena.h and test.h
    exe.addIncludePath(b.path("src")); // Include the src/ directory for headers

    // Link the C standard library
    exe.linkSystemLibrary("c");

    // Install the executable
    b.installArtifact(exe);

    const run_exe = b.addRunArtifact(exe);
    const run_step = b.step("run", "Run the application");
    run_step.dependOn(&run_exe.step);
}
